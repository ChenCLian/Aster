#include "ui/results/record_directory_panel.h"
#include "ui/results/record_item_delegate.h"
#include "ui/panels/record_filter_panel.h"
#include "ui/services/record_directory_service.h"
#include "ui/common/presentation.h"
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
#include <algorithm>
#include <QFileInfo>
#include <QFileDialog>
#include <QProgressDialog>
#include <QDateTime>

namespace aster::ui
{
RecordDirectoryPanel::RecordDirectoryPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("Panel");
    setProperty("panelRole", "navigation");
    setMinimumWidth(200);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 14, 12, 12);
    layout->setSpacing(8);
    auto *title = new QLabel(text("ui.result_directory"));
    title->setObjectName("SectionTitle");
    auto *heading = new QHBoxLayout;
    heading->addWidget(title, 1);
    m_database = new QToolButton;
    m_database->setObjectName("databaseOperations");
    m_database->setText(text("ui.database_operations"));
    m_database->setPopupMode(QToolButton::InstantPopup);
    auto *databaseMenu = new QMenu(m_database);
    auto *mergeAction = databaseMenu->addAction(text("ui.database_merge"), this,
                                                &RecordDirectoryPanel::mergeDatabaseFile);
    mergeAction->setObjectName("mergeDatabaseAction");
    auto *backupAction = databaseMenu->addAction(text("ui.database_backup"), this,
                                                 &RecordDirectoryPanel::backupDatabaseFile);
    backupAction->setObjectName("backupDatabaseAction");
    m_database->setMenu(databaseMenu);
    heading->addWidget(m_database);
    layout->addLayout(heading);
    auto *lookup = new QLineEdit;
    lookup->setObjectName("recordIdLookup");
    lookup->setPlaceholderText(text("ui.record_id_then_enter"));
    layout->addWidget(lookup);
    connect(lookup, &QLineEdit::returnPressed, this,
            [this, lookup]
            {
                bool ok = false;
                const auto id = lookup->text().toLongLong(&ok);
                if (ok && id > 0)
                    emit recordActivated(id);
                else
                    emit message(text("ui.enter_a_valid_record_id"));
            });
    m_filters = new RecordFilterPanel;
    layout->addWidget(m_filters);
    m_list = new QListWidget;
    m_list->setObjectName("resultRecords");
    m_list->setItemDelegate(new RecordItemDelegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->installEventFilter(this);
    m_list->viewport()->installEventFilter(this);
    layout->addWidget(m_list, 1);
    m_count = new QLabel;
    m_count->setObjectName("recordLoadCount");
    layout->addWidget(m_count);
    m_selection = new QLabel;
    m_selection->setWordWrap(true);
    m_selection->setObjectName("recordSelectionCount");
    layout->addWidget(m_selection);
    auto *selectionBar = new QHBoxLayout;
    m_select = new QToolButton;
    m_select->setText(text("ui.check_matches"));
    m_select->setObjectName("checkMatchingRecords");
    m_select->setPopupMode(QToolButton::MenuButtonPopup);
    m_select->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *menu = new QMenu(m_select);
    menu->addAction(text("ui.check_matches"), this, [this] { selectMatching(true); });
    menu->addAction(text("ui.uncheck_matches"), this, [this] { selectMatching(false); });
    m_select->setMenu(menu);
    m_clear = new QPushButton(text("ui.clear_checks"));
    m_clear->setObjectName("clearRecordChecks");
    selectionBar->addWidget(m_select, 1);
    selectionBar->addWidget(m_clear, 1);
    layout->addLayout(selectionBar);
    m_delete = new QPushButton(text("ui.delete_checked"));
    m_delete->setObjectName("deleteCheckedRecords");
    layout->addWidget(m_delete);
    m_service = new RecordDirectoryService(this);
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
                if (result.imported > 0 && m_current > 0)
                    for (const auto id : result.recordIds)
                        if (id == m_current)
                        {
                            emit metadataEdited(m_current);
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
            [this](QString path, RecordSelectionState state, bool checked)
            {
                if (path != m_path)
                    return;
                if (!state.error.isEmpty())
                {
                    emit message(state.error);
                    return;
                }
                for (auto it = state.existing.cbegin(); it != state.existing.cend(); ++it)
                    if (checked)
                        m_checked.insert(it.key(), it.value());
                    else if (m_checked.value(it.key()) == it.value())
                        m_checked.remove(it.key());
                // Inspect the current filter, which may have changed while this request ran.
                refresh();
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
                emit recordsRemoved(result.removed);
                if (result.removed.contains(m_current))
                    m_current = 0;
                refresh();
                emit message(text("ui.records_deleted").arg(result.removed.size()));
            });
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
                    emit recordsRemoved({{id, {}}});
                }
                else
                    emit metadataEdited(id);
                refresh();
            });
    connect(m_select, &QToolButton::clicked, this, [this] { selectMatching(true); });
    connect(m_clear, &QPushButton::clicked, this, &RecordDirectoryPanel::clearChecks);
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
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item)
            { emit recordActivated(item->data(RecordRole::Id).toLongLong()); });
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &RecordDirectoryPanel::fetchIfNeeded);
    connect(m_list, &QListWidget::customContextMenuRequested, this,
            [this](QPoint pos)
            {
                auto *item = m_list->itemAt(pos);
                if (!item)
                    return;
                m_list->setCurrentItem(item);
                QMenu menu(this);
                const auto id = item->data(RecordRole::Id).toLongLong();
                const auto name = item->data(RecordRole::DisplayName).toString(),
                           notes = item->data(RecordRole::Notes).toString();
                auto *edit =
                    menu.addAction(text("ui.edit_name_and_notes"), this,
                                   [this, id, name, notes] { editRecord(id, name, notes); });
                auto *del = menu.addAction(m_checked.isEmpty() ? text("ui.delete_record")
                                                               : text("ui.delete_checked"),
                                           this, &RecordDirectoryPanel::deleteSelection);
                edit->setEnabled(!m_busy && m_mutationsEnabled);
                del->setEnabled(!m_busy && !m_filterPending && m_mutationsEnabled);
                menu.exec(m_list->viewport()->mapToGlobal(pos));
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
void RecordDirectoryPanel::setDatabase(const QString &path)
{
    m_service->resetContext();
    m_path = path;
    m_checked.clear();
    m_matching = 0;
    m_current = m_cursor = m_pressedCheck = 0;
    m_hasMore = false;
    m_list->clear();
    refresh();
}
void RecordDirectoryPanel::setCurrentRecord(qint64 id)
{
    m_current = id;
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
    if (m_path.isEmpty())
        return;
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
    if (!append)
        m_hasMore = false;
    m_service->query(m_path, f, append ? m_cursor : 0, m_checked);
}
void RecordDirectoryPanel::receive(RecordList list, QStringList units, qint64 beforeId)
{
    if (!list.error.isEmpty())
    {
        m_hasMore = false;
        emit message(list.error);
        return;
    }
    m_filters->setUnits(units);
    if (!beforeId)
    {
        m_list->clear();
        m_cursor = 0;
    }
    for (const auto &r : list.records)
    {
        QString label =
            r.displayName.isEmpty() ? QFileInfo(r.sourceLabel).fileName() : r.displayName;
        if (label.isEmpty())
            label = r.sourceLabel;
        auto *item =
            new QListWidgetItem(QString("ID %1 · %2\n%3")
                                    .arg(r.id)
                                    .arg(r.startedAt.toLocalTime().toString("MM-dd hh:mm"), label),
                                m_list);
        item->setData(RecordRole::Id, r.id);
        item->setData(RecordRole::RunId, r.runId);
        item->setData(RecordRole::DisplayName, r.displayName);
        item->setData(RecordRole::Notes, r.notes);
        item->setData(RecordRole::FileName, label);
        item->setData(RecordRole::StartedAt, r.startedAt);
        item->setData(RecordRole::State, r.computationState);
        item->setToolTip(r.sourceLabel + "\n" + r.computationState + "\n" + r.notes);
        item->setData(Qt::AccessibleTextRole, item->text());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        if (r.id == m_current)
            m_list->setCurrentItem(item);
        m_cursor = r.id;
    }
    m_hasMore = list.hasMore;
    syncChecks();
    updateActions();
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
    m_database->setEnabled(!m_busy && m_mutationsEnabled && !m_path.isEmpty());
    m_select->setEnabled(ready && m_filterValid);
    m_clear->setEnabled(ready && !m_checked.isEmpty());
    m_delete->setEnabled(ready && m_filterValid && m_mutationsEnabled && !m_checked.isEmpty());
    m_count->setText(
        m_busy ? text("ui.directory_loading")
               : (m_hasMore ? text("ui.records_loaded_more") : text("ui.records_loaded_all"))
                     .arg(m_list->count()));
    m_selection->setText(text("ui.records_checked_count")
                             .arg(m_checked.size())
                             .arg(std::max<qint64>(0, m_checked.size() - m_matching)));
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
void RecordDirectoryPanel::selectMatching(bool checked)
{
    if (m_busy || m_filterPending || m_path.isEmpty())
        return;
    const auto f = filter();
    const auto error = f.validate();
    if (!error.isEmpty())
    {
        emit message(error);
        return;
    }
    m_service->selectMatching(m_path, f, checked);
}
void RecordDirectoryPanel::clearChecks()
{
    if (!m_busy)
    {
        m_checked.clear();
        m_matching = 0;
        syncChecks();
        updateActions();
    }
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
        if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier)
        {
            if (!key->isAutoRepeat())
                toggleCheck(m_list->currentItem());
            return true;
        }
        if (key->matches(QKeySequence::SelectAll))
        {
            selectMatching(true);
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
                const bool hit = item && RecordItemDelegate::checkRect(m_list->visualItemRect(item))
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
