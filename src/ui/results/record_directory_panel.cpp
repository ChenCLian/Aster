// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/results/record_directory_panel.h"
#include "ui/results/record_item_delegate.h"
#include "ui/panels/record_filter_panel.h"
#include "ui/services/record_directory_service.h"
#include "ui/common/presentation.h"
#include "ui/common/wrapping_button.h"
#include "ui/common/popup_placement.h"
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QTimer>
#include <QKeyEvent>
#include <QKeySequence>
#include <QDialog>
#include <QMouseEvent>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QPersistentModelIndex>
#include <algorithm>
#include <QFileInfo>
#include <QFileDialog>
#include <QProgressDialog>
#include <QDateTime>
#include <QApplication>

namespace aster::ui
{
RecordDirectoryPanel::RecordDirectoryPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("Panel");
    setProperty("panelRole", "navigation");
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(200);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 8);
    layout->setSpacing(5);
    auto *title = new QLabel(text("ui.result_directory"));
    title->setObjectName("SectionTitle");
    title->setWordWrap(true);
    title->setMinimumWidth(0);
    auto *heading = new QHBoxLayout;
    heading->addWidget(title, 1);
    m_database = new QToolButton;
    m_database->setObjectName("databaseOperations");
    m_database->setText(text("ui.database_operations"));
    m_database->setPopupMode(QToolButton::InstantPopup);
    auto *databaseMenu = new QMenu(m_database);
    configureMenu(databaseMenu);
    auto *mergeAction = databaseMenu->addAction(text("ui.database_merge"), this,
                                                &RecordDirectoryPanel::mergeDatabaseFile);
    mergeAction->setObjectName("mergeDatabaseAction");
    auto *backupAction = databaseMenu->addAction(text("ui.database_backup"), this,
                                                 &RecordDirectoryPanel::backupDatabaseFile);
    backupAction->setObjectName("backupDatabaseAction");
    m_database->setMenu(databaseMenu);
    heading->addWidget(m_database);
    layout->addLayout(heading);
    m_filters = new RecordFilterPanel;
    connect(m_filters, &RecordFilterPanel::idSubmitted, this, [this](qint64 id) {
        m_openAfterSearch = id;
        refresh();
    });
    layout->addWidget(m_filters);
    m_noMatch = new QLabel;
    m_noMatch->setObjectName("recordLookupEmpty");
    m_noMatch->setTextFormat(Qt::PlainText);
    m_noMatch->setWordWrap(true);
    m_noMatch->hide();
    layout->addWidget(m_noMatch);
    m_list = new QListWidget;
    m_list->setObjectName("resultRecords");
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->installEventFilter(this);
    m_list->viewport()->installEventFilter(this);
    // The delegate observes input modality before the panel handles checkbox and shortcut events.
    m_delegate = new RecordItemDelegate(m_list);
    m_list->setItemDelegate(m_delegate);
    layout->addWidget(m_list, 1);
    m_selection = new QLabel;
    m_selection->setObjectName("recordSelectionCount");
    m_selection->setMinimumWidth(0);
    m_selection->setWordWrap(true);
    m_selection->setToolTip(text("ui.record_selection_total_hint"));
    layout->addWidget(m_selection);
    auto *selectionActions = new QHBoxLayout;
    m_selectAll = new WrappingPushButton(text("ui.check_database_short"));
    m_selectAll->setObjectName("checkAllRecords");
    m_selectFiltered = new WrappingPushButton(text("ui.check_matches_short"));
    m_selectFiltered->setObjectName("checkMatchingRecords");
    for (auto *button : {m_selectAll, m_selectFiltered}) {
        button->setCheckable(true);
        button->setProperty("selectionAction", true);
        selectionActions->addWidget(button, 1);
        const auto scope = button == m_selectAll ? RecordSelectionScope::Database
                                                : RecordSelectionScope::Filtered;
        connect(button, &QPushButton::clicked, this, [this, button, scope](bool checked) {
            // Commit this command button only when its own operation succeeds.
            button->setChecked(!checked);
            selectRecords(scope, checked);
        });
    }
    layout->addLayout(selectionActions);

    auto *actions = new QHBoxLayout;
    m_exportNames = new QPushButton(text("ui.export_record_names"));
    m_exportNames->setObjectName("exportRecordNames");
    m_exportNames->setToolTip(text("ui.export_record_names_hint"));
    actions->addWidget(m_exportNames, 1);
    m_delete = new QPushButton(text("ui.delete_checked"));
    m_delete->setObjectName("deleteCheckedRecords");
    actions->addWidget(m_delete, 1);
    layout->addLayout(actions);
    m_service = new RecordDirectoryService(this);
    connect(m_exportNames, &QPushButton::clicked, this, &RecordDirectoryPanel::exportFileNames);
    connect(m_service, &RecordDirectoryService::namesExported, this,
            [this](const QString &file, qint64 count, const QString &error) {
                const auto detail = error.isEmpty()
                    ? text("ui.export_record_names_done").arg(count).arg(file)
                    : text("ui.export_record_names_failed").arg(error);
                emit message(detail);
                if (error.isEmpty()) QMessageBox::information(this, text("ui.export_record_names"), detail);
                else QMessageBox::warning(this, text("ui.export_record_names"), detail);
            });
    connect(m_service, &RecordDirectoryService::transferProgress, this,
            [this](const QString &stage, qint64 done, qint64 total)
            {
                if (!m_transferProgress || m_transferProgress->wasCanceled())
                    return;
                auto label = text(("ui.database_stage_" + stage).toUtf8().constData());
                if (total > 0)
                    label += QString(" (%1 / %2)").arg(done).arg(total);
                m_transferProgress->setLabelText(label);
            });
    connect(
        m_service, &RecordDirectoryService::transferred, this,
        [this](const QString &path, const DatabaseTransferResult &result, bool merge)
        {
            if (m_transferProgress)
            {
                m_transferProgress->reset();
                m_transferProgress->deleteLater();
                m_transferProgress = nullptr;
            }
            if (path != m_path)
                return;
            QString messageText;
            if (result.cancelled)
                messageText = text("ui.database_transfer_cancelled");
            else if (!result.ok())
                messageText = text("ui.database_transfer_failed") + "\n" + result.error;
            else if (merge)
                messageText =
                    text("ui.database_merge_done").arg(result.imported).arg(result.duplicates);
            else
                messageText = text("ui.database_backup_done");
            if (result.externalReferences > 0)
                messageText +=
                    "\n" + text("ui.database_external_references").arg(result.externalReferences);
            if (!result.backupPath.isEmpty())
                messageText += "\n\n" + text("ui.database_backup_location").arg(result.backupPath);
            if (merge && result.ok())
            {
                refresh();
                // Matching owners may now use a source imported into this database.
                const auto opened = m_delegate->openedRecord();
                if (result.imported > 0 && opened > 0)
                    for (const auto id : result.recordIds)
                        if (id == opened)
                        {
                            emit metadataEdited(opened);
                            break;
                        }
            }
            emit message(messageText);
            if (result.ok())
                QMessageBox::information(this, text("ui.database_operations"), messageText);
            else if (!result.cancelled)
                QMessageBox::warning(this, text("ui.database_operations"), messageText);
        });
    connect(m_service, &RecordDirectoryService::mutationBusyChanged, this,
            &RecordDirectoryPanel::mutationBusyChanged);
    connect(m_filters, &RecordFilterPanel::editing, this,
            [this]
            {
                m_openAfterSearch = -1;
                m_noMatch->hide();
                m_filterPending = true;
                m_service->invalidate();
                m_hasMore = false;
                updateActions();
            });
    connect(m_filters, &RecordFilterPanel::changed, this, &RecordDirectoryPanel::refresh);
    connect(m_service, &RecordDirectoryService::listed, this, &RecordDirectoryPanel::receive);
    connect(m_service, &RecordDirectoryService::selectionInspected, this,
            [this](RecordSelectionState state)
            {
                m_checked = std::move(state.existing);
                m_matching = state.matching;
            });
    connect(m_service, &RecordDirectoryService::busyChanged, this,
            [this](bool busy)
            {
                m_busy = busy;
                updateActions();
                emit busyChanged(busy);
                if (!busy)
                    QTimer::singleShot(0, this, &RecordDirectoryPanel::fetchIfNeeded);
            });
    connect(m_service, &RecordDirectoryService::selectionReady, this,
            [this](QString path, RecordSelectionState state, bool checked, RecordSelectionScope scope)
            {
                if (path != m_path) return;
                if (!state.error.isEmpty()) {
                    emit message(state.error);
                    return;
                }
                const bool database = scope == RecordSelectionScope::Database;
                (database ? m_selectAll : m_selectFiltered)->setChecked(checked);
                bool changed = false;
                if (database) {
                    m_databaseTotal = state.existing.size();
                    changed = m_checked != state.existing;
                    m_checked = std::move(state.existing);
                } else {
                    for (auto it = state.existing.cbegin(); it != state.existing.cend(); ++it) {
                        const bool present = m_checked.value(it.key()) == it.value();
                        if (checked && !present) { m_checked.insert(it.key(), it.value()); changed = true; }
                        if (!checked && present) { m_checked.remove(it.key()); changed = true; }
                    }
                }
                if (changed) {
                    syncChecks();
                    // A filter change already owes the user a new first page. A change
                    // to checkboxes alone only needs fresh counts and checked identities.
                    if (!m_filterPending && m_filterValid) {
                        if (m_pagePending) refresh();
                        else m_service->query(m_path, filter(), 0, m_checked, true);
                    }
                }
                updateActions();
            });
    connect(m_service, &RecordDirectoryService::removed, this,
            [this](QString path, RecordRemoval result)
            {
                if (path != m_path)
                    return;
                if (!result.ok())
                {
                    const auto error = result.blockedSource ? text("ui.delete_dependency_blocked")
                                                                  .arg(result.blockedSource)
                                                                  .arg(result.blockingOwner)
                                                            : result.error;
                    QMessageBox::warning(this, text("ui.delete_records_title"), error);
                    emit message(error);
                    return;
                }
                for (auto it = result.removed.cbegin(); it != result.removed.cend(); ++it)
                    m_checked.remove(it.key());
                if (result.removed.contains(m_delegate->openedRecord()))
                    setOpenedRecord(0);
                emit recordsRemoved(result.removed);
                refresh();
                emit message(text("ui.records_deleted").arg(result.removed.size()));
            });
    connect(m_service, &RecordDirectoryService::recordNameChanged, this, &RecordDirectoryPanel::recordNameChanged);
    connect(m_service, &RecordDirectoryService::edited, this,
            [this](QString path, qint64 id, bool removed, QString error)
            {
                if (path != m_path)
                    return;
                if (!error.isEmpty())
                {
                    emit message(error);
                    return;
                }
                if (removed)
                {
                    m_checked.remove(id);
                    if (id == m_delegate->openedRecord())
                        setOpenedRecord(0);
                    emit recordsRemoved({{id, {}}});
                }
                else
                    emit metadataEdited(id);
                refresh();
            });
    connect(m_delete, &QPushButton::clicked, this, [this] { deleteRecords(m_checked); });
    connect(m_list, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item)
            {
                if (!item->data(Qt::CheckStateRole).isValid())
                    return;
                if (m_busy || m_filterPending || !m_filterValid)
                {
                    syncChecks();
                    return;
                }
                const auto ids = identity(item);
                const auto id = ids.firstKey();
                if (item->checkState() == Qt::Checked && !m_checked.contains(id))
                {
                    m_checked.insert(id, ids.first());
                    ++m_matching;
                }
                else if (item->checkState() != Qt::Checked && m_checked.remove(id))
                    --m_matching;
                updateActions();
            });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!m_filterPending && !m_pagePending && m_filterValid)
            emit recordActivated(item->data(RecordRole::Id).toLongLong());
    });
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &RecordDirectoryPanel::fetchIfNeeded);
    connect(m_list, &QListWidget::customContextMenuRequested, this,
            [this](QPoint pos)
            {
                const bool keyboard = pos.x() < 0 || pos.y() < 0;
                auto *item = keyboard ? m_list->currentItem() : m_list->itemAt(pos);
                if (!item) return;
                m_list->setCurrentItem(item);
                const QPersistentModelIndex index(m_list->indexFromItem(item));
                const QPoint anchor = keyboard ? m_list->visualItemRect(item).center() : pos;
                QMenu menu(this);
                const auto id = item->data(RecordRole::Id).toLongLong();
                const auto name = item->data(RecordRole::DisplayName).toString(),
                           notes = item->data(RecordRole::Notes).toString();
                const bool wasChecked = item->checkState() == Qt::Checked;
                auto *check = menu.addAction(text(wasChecked ? "ui.uncheck_this_record" : "ui.check_this_record"));
                check->setObjectName("toggleRecordCheck");
                check->setEnabled(!m_busy && !m_filterPending && m_filterValid);
                auto *edit = menu.addAction(text("ui.edit_name_and_notes"));
                edit->setEnabled(!m_busy && m_mutationsEnabled);
                const auto *chosen = menu.exec(menuPopupPosition(&menu, this,
                    QRect(m_list->viewport()->mapToGlobal(anchor), QSize(1, 1))));
                // A refresh can replace the row while the native menu is open.
                if (!index.isValid()) return;
                if (chosen == check && !m_busy && !m_filterPending && m_filterValid)
                    m_list->itemFromIndex(index)->setCheckState(wasChecked ? Qt::Unchecked : Qt::Checked);
                else if (chosen == edit) editRecord(id, name, notes);
            });
    updateActions();
}
int RecordDirectoryPanel::recordCount() const
{
    return m_list->count();
}
RecordFilter RecordDirectoryPanel::filter() const
{
    return m_filters->filter();
}
void RecordDirectoryPanel::setMutationsEnabled(bool enabled)
{
    m_mutationsEnabled = enabled;
    updateActions();
}
bool RecordDirectoryPanel::refreshIfIdle()
{
    // Background saves must not rewind pagination, invalidate a slow filter
    // or steal the controls from a selection or a confirmation dialog.
    if (!isVisible() || m_busy || m_filterPending || !m_filterValid || !m_checked.isEmpty() ||
        m_list->verticalScrollBar()->value() > m_list->verticalScrollBar()->minimum() ||
        QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return false;
    refresh();
    return true;
}
void RecordDirectoryPanel::setDatabase(const QString &path)
{
    m_service->resetContext();
    m_openAfterSearch = -1;
    m_noMatch->hide();
    m_path = path;
    m_checked.clear();
    m_matching = 0;
    m_databaseTotal = -1;
    m_selectAll->setChecked(false);
    m_selectFiltered->setChecked(false);
    m_cursor = m_pressedCheck = 0;
    setOpenedRecord(0);
    m_hasMore = false;
    m_list->clear();
    refresh();
}
void RecordDirectoryPanel::setOpenedRecord(qint64 id)
{
    m_delegate->setOpenedRecord(id);
}
void RecordDirectoryPanel::focusRecord(qint64 id)
{
    if (m_list->currentIndex().data(RecordRole::Id).toLongLong() == id)
        return;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(RecordRole::Id).toLongLong() == id)
        {
            m_list->setCurrentRow(i);
            return;
        }
}
void RecordDirectoryPanel::refresh()
{
    m_filterPending = false;
    load(false);
}
void RecordDirectoryPanel::load(bool append)
{
    if (m_path.isEmpty()) {
        updateActions();
        return;
    }
    const auto f = filter();
    const auto error = f.validate();
    m_filterValid = error.isEmpty();
    if (!m_filterValid)
    {
        m_hasMore = false;
        m_service->invalidate();
        updateActions();
        emit message(error);
        return;
    }
    if (append && (m_busy || !m_hasMore || m_cursor <= 0))
        return;
    if (!append) {
        m_pagePending = true;
        m_hasMore = false;
    }
    m_service->query(m_path, f, append ? m_cursor : 0, m_checked);
}
void RecordDirectoryPanel::receive(RecordList list, QStringList units, qint64 beforeId, bool selectionOnly)
{
    if (!beforeId && !selectionOnly) m_pagePending = false;
    if (!list.error.isEmpty())
    {
        m_openAfterSearch = -1;
        m_noMatch->hide();
        m_hasMore = false;
        m_databaseTotal = -1;
        m_filterValid = false;
        updateActions();
        emit message(list.error);
        return;
    }
    if (selectionOnly) {
        m_databaseTotal = list.totalRecords;
        syncChecks();
        updateActions();
        return;
    }
    const auto exactId = filter().exactRecordId();
    if (!beforeId && !exactId && m_filters->setUnits(units)) {
        // A unit group changed while a range was active. Discard the stale page.
        refresh();
        return;
    }
    const QSignalBlocker block(m_list);
    const bool updates = m_list->updatesEnabled();
    m_list->setUpdatesEnabled(false);
    const qint64 focusId = m_list->currentItem()
        ? m_list->currentItem()->data(RecordRole::Id).toLongLong() : m_delegate->openedRecord();
    if (!beforeId)
    {
        m_list->clear();
        m_cursor = 0;
        m_databaseTotal = list.totalRecords;
    }
    for (const auto &r : list.records)
    {
        QString label =
            r.displayName.isEmpty() ? QFileInfo(r.sourceLabel).fileName() : r.displayName;
        if (label.isEmpty())
            label = r.sourceLabel;
        const auto details = QString("ID %1 · %2").arg(r.id).arg(r.startedAt.toLocalTime().toString("yyyy-MM-dd hh:mm"));
        const auto status = RecordItemDelegate::statusFor(r.computationState);
        auto *item = new FileListItem(label, m_list, {r.sourceLabel, details, r.notes, r.hasWorkflowError}, status);
        item->setData(RecordRole::Id, r.id);
        item->setData(RecordRole::RunId, r.runId);
        item->setData(RecordRole::DisplayName, r.displayName);
        item->setData(RecordRole::Notes, r.notes);
        item->setData(RecordRole::FileName, label);
        item->setData(RecordRole::StartedAt, r.startedAt);
        item->setData(RecordRole::State, r.computationState);
        item->setData(RecordRole::WorkflowError, r.hasWorkflowError);
        if (r.hasWorkflowError)
            item->setData(Qt::AccessibleDescriptionRole, text("ui.record_workflow_error"));
        item->setData(Qt::AccessibleTextRole, label + " · " + details + " · " + text(status.label));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        if (r.id == focusId || exactId == r.id)
            m_list->setCurrentItem(item);
        m_cursor = r.id;
    }
    m_hasMore = list.hasMore;
    // The queued bottom check must see the new scroll range, including after an append.
    m_list->doItemsLayout();
    if (!beforeId) {
        if (auto *current = m_list->currentItem()) m_list->scrollToItem(current);
        else m_list->scrollToTop();
    }
    syncChecks();
    m_list->setUpdatesEnabled(updates);
    const bool missing = exactId && list.records.isEmpty();
    if (missing) m_noMatch->setText(text("ui.record_id_no_match"));
    m_noMatch->setVisible(missing);
    const bool open = exactId && !missing && *exactId == m_openAfterSearch &&
                      *exactId != m_delegate->openedRecord();
    m_openAfterSearch = -1;
    updateActions();
    if (open) emit recordActivated(*exactId);
}
void RecordDirectoryPanel::syncChecks()
{
    QSignalBlocker block(m_list);
    for (int i = 0; i < m_list->count(); ++i)
    {
        auto *item = m_list->item(i);
        const auto id = item->data(RecordRole::Id).toLongLong();
        const auto run = item->data(RecordRole::RunId).toUuid();
        item->setCheckState(m_checked.contains(id) && m_checked.value(id) == run ? Qt::Checked
                                                                                 : Qt::Unchecked);
    }
}
void RecordDirectoryPanel::updateActions()
{
    const bool ready = !m_busy && !m_filterPending && !m_path.isEmpty();
    const bool selected = !m_checked.isEmpty();
    m_delegate->setChecksVisible(selected);
    const auto label = [](QPushButton *button, const char *on, const char *off,
                          const char *onHint, const char *offHint) {
        button->setText(text(button->isChecked() ? off : on));
        button->setToolTip(text(button->isChecked() ? offHint : onHint));
        button->setAccessibleName(button->text());
        button->setAccessibleDescription(button->toolTip());
    };
    label(m_selectAll, "ui.check_database_short", "ui.uncheck_database_short",
          "ui.check_database", "ui.uncheck_database");
    label(m_selectFiltered, "ui.check_matches_short", "ui.uncheck_matches_short",
          "ui.check_matches", "ui.uncheck_matches");
    m_selectAll->setEnabled(ready && (m_databaseTotal != 0 || selected));
    m_selectFiltered->setEnabled(ready && m_filterValid && m_list->count() > 0);
    m_database->setEnabled(!m_busy && m_mutationsEnabled && !m_path.isEmpty());
    m_exportNames->setEnabled(ready && selected);
    m_delete->setEnabled(ready && m_filterValid && m_mutationsEnabled && selected);
    if (m_databaseTotal >= 0)
        m_selection->setText(text("ui.records_selected_of").arg(m_checked.size()).arg(m_databaseTotal));
    else
        m_selection->setText(m_busy ? text("ui.directory_loading") : QString());
}
void RecordDirectoryPanel::exportFileNames()
{
    const auto source = m_path;
    const auto selected = m_checked;
    if (source.isEmpty() || selected.isEmpty()) return;
    QFileDialog dialog(this, text("ui.export_record_names"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter("CSV (*.csv)");
    dialog.setDefaultSuffix("csv");
    dialog.selectFile(QString("AstroBatch_file_index_%1.csv")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    if (m_path != source) return;
    m_service->exportNames(source, selected, dialog.selectedFiles().front());
}
void RecordDirectoryPanel::fetchIfNeeded()
{
    if (!isVisible() || m_busy || m_filterPending || !m_hasMore || !m_filterValid)
        return;
    // The asynchronous page can arrive before QListView updates its scroll
    // range. Complete that layout before deciding whether the bottom is visible.
    auto *bar = m_list->verticalScrollBar();
    if (bar->maximum() == 0 && m_list->count() > 0)
        m_list->doItemsLayout();
    if (bar->value() >= bar->maximum())
        load(true);
}
void RecordDirectoryPanel::selectRecords(RecordSelectionScope scope, bool checked)
{
    if (m_busy || m_filterPending || m_path.isEmpty()) return;
    if (scope == RecordSelectionScope::Database && !checked) {
        m_checked.clear();
        m_matching = 0;
        m_selectAll->setChecked(false);
        syncChecks();
        updateActions();
        return;
    }
    const auto f = scope == RecordSelectionScope::Database ? RecordFilter{} : filter();
    const auto error = f.validate();
    if (!error.isEmpty())
    {
        emit message(error);
        return;
    }
    m_service->selectMatching(m_path, f, checked, scope);
}
RecordIdentities RecordDirectoryPanel::identity(QListWidgetItem *item) const
{
    if (!item)
        return {};
    return {{item->data(RecordRole::Id).toLongLong(), item->data(RecordRole::RunId).toUuid()}};
}
void RecordDirectoryPanel::toggleCheck(QListWidgetItem *item)
{
    if (!item || m_busy || m_filterPending || !m_filterValid)
        return;
    item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}
void RecordDirectoryPanel::deleteSelection()
{
    deleteRecords(m_checked.isEmpty() ? identity(m_list->currentItem()) : m_checked);
}
void RecordDirectoryPanel::deleteRecords(RecordIdentities records)
{
    if (m_busy || m_filterPending || !m_filterValid || !m_mutationsEnabled || records.isEmpty())
        return;
    const auto path = m_path;
    const auto hidden =
        records == m_checked ? std::max<qint64>(0, m_checked.size() - m_matching) : 0;
    QMessageBox box(QMessageBox::Warning, text("ui.delete_records_title"),
                    text("ui.delete_records_confirm").arg(records.size()).arg(hidden),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    QStringList ids;
    for (auto it = records.cbegin(); it != records.cend() && ids.size() < 200; ++it)
        ids << QString::number(it.key());
    if (records.size() > 200)
        ids << text("ui.more_selected_ids").arg(records.size() - 200);
    box.setDetailedText("ID: " + ids.join(", "));
    if (box.exec() != QMessageBox::Yes || path != m_path || m_busy || m_filterPending ||
        !m_mutationsEnabled)
        return;
    m_service->removeMany(path, std::move(records));
}
void RecordDirectoryPanel::editRecord(qint64 id, QString oldName, QString oldNotes)
{
    if (m_busy || !m_mutationsEnabled)
        return;
    const auto path = m_path;
    QDialog dialog(this);
    dialog.setWindowTitle(text("ui.name_and_notes"));
    auto *layout = new QFormLayout(&dialog);
    auto *name = new QLineEdit(oldName);
    auto *notes = new QPlainTextEdit(oldNotes);
    layout->addRow(text("ui.display_name"), name);
    layout->addRow(text("ui.notes"), notes);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted && path == m_path && !m_busy && m_mutationsEnabled)
        m_service->edit(path, id, name->text(), notes->toPlainText());
}
bool RecordDirectoryPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Delete && key->modifiers() == Qt::NoModifier)
        {
            if (!key->isAutoRepeat())
                deleteSelection();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            !(key->modifiers() & ~Qt::KeypadModifier)) {
            const auto *item = m_list->currentItem();
            if (!key->isAutoRepeat() && item && !m_busy && !m_filterPending && m_filterValid)
                emit recordActivated(item->data(RecordRole::Id).toLongLong());
            return true;
        }
        if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier)
        {
            if (!key->isAutoRepeat())
                toggleCheck(m_list->currentItem());
            return true;
        }
        if (key->matches(QKeySequence::SelectAll))
        {
            selectRecords(RecordSelectionScope::Filtered, true);
            return true;
        }
    }
    if (watched == m_list->viewport())
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
            QTimer::singleShot(0, this, &RecordDirectoryPanel::fetchIfNeeded);
        if (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonRelease ||
            event->type() == QEvent::MouseButtonDblClick)
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton)
            {
                auto *item = m_list->itemAt(mouse->pos());
                const bool hit = !m_checked.isEmpty() && item && RecordItemDelegate::checkRect(m_list->visualItemRect(item))
                                             .contains(mouse->pos());
                if (event->type() == QEvent::MouseButtonPress && hit)
                {
                    m_pressedCheck = item->data(RecordRole::Id).toLongLong();
                    m_list->setCurrentItem(item);
                    m_list->setFocus();
                    return true;
                }
                if (event->type() == QEvent::MouseButtonDblClick && hit)
                    return true;
                if (event->type() == QEvent::MouseButtonRelease && (m_pressedCheck || hit))
                {
                    const auto pressed = m_pressedCheck;
                    m_pressedCheck = 0;
                    if (pressed && hit && item->data(RecordRole::Id).toLongLong() == pressed)
                        toggleCheck(item);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
void RecordDirectoryPanel::showTransferProgress()
{
    m_transferProgress = new QProgressDialog(text("ui.database_stage_checking"),
                                             text("ui.database_cancel"), 0, 0, this);
    m_transferProgress->setObjectName("databaseTransferProgress");
    m_transferProgress->setWindowTitle(text("ui.database_operations"));
    m_transferProgress->setWindowModality(Qt::WindowModal);
    m_transferProgress->setAutoClose(false);
    m_transferProgress->setAutoReset(false);
    m_transferProgress->setMinimumDuration(0);
    connect(m_transferProgress, &QProgressDialog::canceled, this,
            [this]
            {
                m_service->cancelTransfer();
                emit message(text("ui.database_cancelling"));
            });
    m_transferProgress->show();
}
void RecordDirectoryPanel::mergeDatabaseFile()
{
    if (m_busy || !m_mutationsEnabled || m_path.isEmpty())
        return;
    const auto destination = m_path;
    const auto source = QFileDialog::getOpenFileName(this, text("ui.database_merge"), {},
                                                     text("ui.database_file_filter"));
    if (source.isEmpty())
        return;
    if (QMessageBox::question(this, text("ui.database_merge"),
                              text("ui.database_merge_confirm").arg(source, destination),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    // A workflow could have started during the native dialog's nested event loop.
    if (m_busy || !m_mutationsEnabled || m_path != destination)
        return;
    showTransferProgress();
    m_service->merge(destination, source);
}
void RecordDirectoryPanel::backupDatabaseFile()
{
    if (m_busy || !m_mutationsEnabled || m_path.isEmpty())
        return;
    const auto source = m_path;
    const auto proposed = QFileInfo(source).absolutePath() + "/" +
                          QFileInfo(m_path).completeBaseName() + "-backup-" +
                          QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss") + ".sqlite";
    const auto path = QFileDialog::getSaveFileName(this, text("ui.database_backup"), proposed,
                                                   text("ui.database_file_filter"), nullptr,
                                                   QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty() || m_busy || !m_mutationsEnabled || m_path != source)
        return;
    if (QFileInfo::exists(path))
    {
        QMessageBox::warning(this, text("ui.database_backup"), text("ui.database_backup_exists"));
        return;
    }
    showTransferProgress();
    m_service->backup(source, path);
}
} // namespace aster::ui
