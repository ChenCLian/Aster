// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
#include "ui/settings/theme_manager.h"
#include "ui/panels/source_panel.h"
#include "ui/common/presentation.h"
#include "ui/panels/file_options_editor.h"
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QItemSelectionModel>
#include <QSet>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include "ui/common/section_splitter.h"
#include <algorithm>
#include <QThread>
#include <QScopedValueRollback>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QToolButton>
#include "ui/help/help_dialog.h"
namespace aster::ui
{
namespace
{
QString stateText(SourceFileState state)
{
    switch (state)
    {
    case SourceFileState::Running: return text("ui.running");
    case SourceFileState::Saved: return text("ui.complete_saved");
    case SourceFileState::Unsaved: return text("ui.complete_unsaved");
    case SourceFileState::Cancelled: return text("ui.cancelled");
    case SourceFileState::Failed: return text("ui.failed_partial");
    default: return text("ui.ready");
    }
}
void updateFileState(QListWidgetItem *item, SourceFileState state)
{
    const auto label = stateText(state);
    item->setData(Qt::UserRole + 1, label);
    item->setData(Qt::UserRole + 2, int(state));
    const auto details = item->data(Qt::UserRole + 3).toString();
    item->setToolTip("<p>" + details.toHtmlEscaped().replace("\n", "<br>") +
                     "<br>" + label.toHtmlEscaped() + "</p>");
    item->setData(Qt::AccessibleTextRole, item->text());
    item->setData(Qt::AccessibleDescriptionRole, details + "\n" + label);
}
class SourceFileDelegate : public QStyledItemDelegate
{
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        auto opt = option;
        initStyleOption(&opt, index);
        const auto name = opt.text;
        opt.text.clear();
        const auto *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        painter->save();
        painter->setFont(opt.font);
        painter->setClipRect(opt.rect);
        painter->setRenderHint(QPainter::Antialiasing);
        const auto state = SourceFileState(index.data(Qt::UserRole + 2).toInt());
        const char *role = state == SourceFileState::Running ? "running" :
                           state == SourceFileState::Saved ? "success" :
                           state == SourceFileState::Failed ? "error" :
                           state == SourceFileState::Unsaved ? "warning" : "muted";
        painter->setPen(Qt::NoPen);
        painter->setBrush(themeColor(role));
        const QPointF center(opt.rect.left() + 11, opt.rect.center().y());
        // Hollow marks distinguish pending/cancelled inputs without relying on color.
        if (state == SourceFileState::Pending || state == SourceFileState::Cancelled)
        { painter->setBrush(Qt::NoBrush); painter->setPen(QPen(themeColor(role), 1.4)); }
        painter->drawEllipse(center, 3.5, 3.5);
        const auto rect = opt.rect.adjusted(23, 3, -7, -3);
        painter->setPen(themeColor((opt.state & QStyle::State_Selected) ? "selectionText" : "text"));
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter,
                          opt.fontMetrics.elidedText(name, Qt::ElideMiddle, rect.width()));
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return {100, std::max(32, option.fontMetrics.height() + 14)};
    }
};
} // namespace
SourcePanel::SourcePanel(WorkflowDocument *d, QWidget *p) : QWidget(p), m_document(d)
{
    setObjectName("Panel");
    setProperty("panelRole", "navigation");
    setAttribute(Qt::WA_StyledBackground);
    setAcceptDrops(true);
    setMinimumWidth(220);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(text("ui.input_data"));
    title->setObjectName("SectionTitle");
    header->addWidget(title, 1);
    auto *add = new QPushButton(text("ui.import_files_short"));
    add->setObjectName("importFiles");
    header->addWidget(add);
    layout->addLayout(header);
    m_roots = new ChoiceEditor;
    m_roots->setObjectName("sourceRoot");
    m_roots->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_roots->setMinimumContentsLength(6);
    m_roots->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(m_roots);
    connect(add, &QPushButton::clicked, this, &SourcePanel::importRequested);
    // Keep the file list and its options independently resizable.
    auto *splitter = new SectionSplitter(Qt::Vertical);
    splitter->setObjectName("sourceOptionsSplitter");
    splitter->setChildrenCollapsible(false);
    auto *filesPanel = new QWidget;
    auto *filesLayout = new QVBoxLayout(filesPanel);
    filesLayout->setContentsMargins(0, 0, 0, 0);
    filesLayout->setSpacing(6);
    m_files = new QListWidget;
    m_files->setObjectName("sourceFiles");
    m_files->setItemDelegate(new SourceFileDelegate(m_files));
    m_files->setUniformItemSizes(true);
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_files->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_files->setTextElideMode(Qt::ElideMiddle);
    m_files->setMinimumHeight(150);
    filesLayout->addWidget(m_files, 1);
    m_count = new QLabel;
    m_count->setObjectName("Muted");
    // Keep localized file counts from squeezing the action button labels.
    filesLayout->addWidget(m_count);
    auto *fileActions = new QHBoxLayout;
    fileActions->addStretch(1);
    auto *remove = new QPushButton(text("ui.remove_short"));
    remove->setToolTip(text("ui.remove_selected_inputs"));
    remove->setObjectName("removeSourceInput");
    auto *move = new QPushButton(text("ui.move_inputs"));
    move->setObjectName("moveSourceInputs");
    move->setToolTip(text("ui.move_inputs_hint"));
    fileActions->addWidget(move);
    connect(move,&QPushButton::clicked,this,&SourcePanel::moveInputsRequested);
    fileActions->addWidget(remove);
    auto *help = new QToolButton;
    help->setText("?"); help->setObjectName("sourceSelectionHelp");
    help->setToolTip(text("ui.input_selection_help"));
    help->setAccessibleName(text("ui.input_selection_help"));
    fileActions->addWidget(help);
    connect(help, &QToolButton::clicked, this, [this] { openHelp(window(), userGuideTopic("input-selection")); });
    filesLayout->addLayout(fileActions);
    m_selectionHint = new QLabel;
    m_selectionHint->setObjectName("sourceSelectionHint");
    m_selectionHint->setWordWrap(true);
    filesLayout->addWidget(m_selectionHint);
    splitter->addWidget(filesPanel);
    m_options = new FileOptionsEditor;
    connect(m_options, &FileOptionsEditor::previewInputChanged,
            this, [this](QString path, QJsonObject options, bool ready) {
                emit previewInputChanged(path, options, ready);
                notifyPreviewSource();
            });
    m_options->setMinimumHeight(180);
    splitter->addWidget(m_options);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({420, 300});
    layout->addWidget(splitter, 1);
    connect(m_options, &FileOptionsEditor::readinessChanged, this,
            [this] { emit readinessChanged(readyToRun()); });
    connect(m_files, &QListWidget::currentItemChanged, this, [this] { showFileOptions(); });
    connect(m_files, &QListWidget::itemSelectionChanged, this, &SourcePanel::updateSelectionHint);
    connect(m_options, &FileOptionsEditor::editStarted, this, &SourcePanel::captureOptionsSelection);
    connect(m_options, &FileOptionsEditor::optionsEdited, this,
            [this](const QString &path, const QJsonObject &options) {
                auto edit = m_optionsEdit;
                if (!edit.paths.contains(path)) return;
                edit.options = options;
                bool changed = false;
                for (const auto &target : edit.paths) changed |= edit.before.value(target) != options;
                if (!changed) return;
                if (edit.paths.size() == 1 && !m_optionsWorker && m_optionsQueue.isEmpty()) {
                    QUuid currentId;
                    for (const auto &dataset : m_document->datasets(edit.root))
                        if (dataset.source.provenance["path"].toString() == path) currentId = dataset.id;
                    QScopedValueRollback<bool> committing(m_committingOptions, true);
                    if (currentId.isNull() || currentId != edit.identities.value(path) ||
                        m_document->fileOptions(edit.root,path) != edit.before.value(path) ||
                        !m_document->setFileOptions(edit.root,{path},options))
                        emit editRejected(text("ui.file_options_not_applied"));
                    return;
                }
                m_optionsQueue.enqueue(edit);
                startOptionsEdit();
                emit readinessChanged(readyToRun());
            });
    connect(remove, &QPushButton::clicked, this,
            [this]
            {
                m_options->flushEdits();
                if (m_optionsWorker || !m_optionsQueue.isEmpty()) return;
                auto paths = m_document->files(root());
                QSet<QString> selected;
                for (auto *item : m_files->selectedItems())
                    selected.insert(item->data(Qt::UserRole).toString());
                paths.erase(std::remove_if(paths.begin(),paths.end(),
                    [&](const QString &path) { return selected.contains(path); }),paths.end());
                m_document->setFiles(root(), paths);
            });
    connect(m_roots, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refresh(); });
    connect(m_document, &WorkflowDocument::changed, this, [this] {
        QVector<NodeId> roots;
        for (const auto &node:m_document->model().definition().nodes)
            if (!node.input && m_document->model().registry().find(node.method)->descriptor.requiresSourceBinding)
                roots.push_back(node.id);
        bool same=roots.size()==m_roots->count();
        for (int i=0;same && i<roots.size();++i) same=roots[i]==m_roots->itemData(i).toUuid();
        // Editing a task parameter does not rebuild thousands of input rows.
        if (!same) refresh();
    });
    connect(m_document, &WorkflowDocument::sourcesChanged, this, &SourcePanel::refresh);
    refresh();
}
void SourcePanel::moveSelectedDatasets()
{
    if (m_document->editingLocked()) return;
    m_options->flushEdits();
    if (m_optionsWorker || !m_optionsQueue.isEmpty()) return;
    const auto sourceRoot=root();
    const auto bound=m_document->sources().value(sourceRoot);
    QVector<QUuid> selected;
    for (auto *item : m_files->selectedItems()) {
        const int row=m_files->row(item);
        if (row>=0 && row<bound.size()) selected<<QUuid(bound[row].provenance["dataset_id"].toString());
    }
    if (selected.isEmpty()) return;
    auto *dialog=new QDialog(this);
    dialog->setObjectName("moveInputsDialog"); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(text("ui.move_inputs_title")); dialog->setWindowModality(Qt::WindowModal);
    auto *layout=new QVBoxLayout(dialog);
    auto *heading=new QLabel(text("ui.move_inputs_destination").arg(selected.size()));
    heading->setWordWrap(true); layout->addWidget(heading);
    auto *target=new ChoiceEditor; target->setObjectName("moveInputsTarget");
    target->addItem(text("ui.move_inputs_new_tree"),QUuid{});
    int number=0;
    for (const auto &n : m_document->model().definition().nodes)
        if (!n.input && n.method=="file_source") {
            ++number;
            if (n.id!=sourceRoot) target->addItem(methodLabel(m_document->model().registry().find(n.method)->descriptor)+" "+QString::number(number),n.id);
        }
    layout->addWidget(target);
    auto *copy=new QCheckBox(text("ui.move_inputs_copy_steps")); copy->setObjectName("moveInputsCopySteps");
    copy->setChecked(true); layout->addWidget(copy);
    auto *hint=new QLabel(text("ui.move_inputs_copy_hint")); hint->setWordWrap(true); layout->addWidget(hint);
    auto *error=new QLabel; error->setWordWrap(true); error->setTextFormat(Qt::PlainText); layout->addWidget(error);
    connect(target,qOverload<int>(&QComboBox::currentIndexChanged),dialog,[target,copy] { copy->setEnabled(target->currentData().toUuid().isNull()); });
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout->addWidget(buttons);
    const auto revision=m_document->revision();
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,target,copy,error,sourceRoot,selected,revision] {
        if (revision!=m_document->revision()) { error->setText(text("ui.move_inputs_changed")); return; }
        const auto result=m_document->moveDatasets(sourceRoot,target->currentData().toUuid(),selected,copy->isChecked());
        if (!result.ok()) { error->setText(result.error); return; }
        selectDataset(result.nodeId,selected.front());
        dialog->accept();
        emit inputsMoved(result.nodeId);
    });
    dialog->resize(430,240); dialog->open();
}
SourcePanel::~SourcePanel()
{
    disconnect(m_options, nullptr, this, nullptr);
    m_optionsCancel.cancel();
    if (m_optionsWorker) { m_optionsWorker->wait(); delete m_optionsWorker; }
}
void SourcePanel::updateSelectionHint()
{
    m_selectionHint->setText(text("ui.input_selection_scope").arg(m_files->selectedItems().size()));
    m_options->setEnabled(m_files->currentItem() && m_files->currentItem()->isSelected());
}
void SourcePanel::captureOptionsSelection()
{
    m_optionsEdit = {};
    m_optionsEdit.root = m_optionsRoot;
    QMap<QString, QJsonObject> optionsByPath;
    for (const auto &source : m_document->sources().value(m_optionsRoot)) {
        const auto path = source.provenance["path"].toString();
        m_optionsEdit.identities.insert(path, QUuid(source.provenance["dataset_id"].toString()));
        optionsByPath.insert(path, source.provenance["file_options"].toObject());
    }
    for (auto *item : m_files->selectedItems()) {
        const auto path = item->data(Qt::UserRole).toString();
        m_optionsEdit.paths << path;
        // One snapshot scan, rather than a full source-list scan per selection.
        m_optionsEdit.before[path] = optionsByPath.value(path);
    }
    // A current row may remain focused after Ctrl-click deselects it. The
    // editor is disabled in that case, rather than silently editing another row.
}
void SourcePanel::startOptionsEdit()
{
    if (m_optionsWorker || m_optionsQueue.isEmpty()) return;
    const auto edit = m_optionsQueue.dequeue();
    const auto rootId = edit.root;
    const auto paths = edit.paths;
    const auto before = edit.before;
    const auto identities = edit.identities;
    const auto options = edit.options;
    SeriesFileOptions checked;
    if (!applyFileOptions(options,checked).isEmpty()) { startOptionsEdit(); return; }
    m_optionsCancel = CancellationToken{};
    findChild<QPushButton *>("moveSourceInputs")->setEnabled(false);
    findChild<QPushButton *>("removeSourceInput")->setEnabled(false);
    const auto cancellation = m_optionsCancel;
    // Read one file at a time. The numeric data is discarded after validation;
    // only compatible paths and explanations cross back to the GUI thread.
    m_optionsWorker = QThread::create([this,rootId,paths,before,identities,options,checked,cancellation] {
        QStringList valid, skipped;
        for (const auto &path : paths) {
            if (cancellation.isCancelled()) return;
            // Single-file edits retain the existing immediate preview validation;
            // bulk edits additionally validate each complete input before committing.
            if (paths.size() == 1) { valid << path; continue; }
            const auto probe = probeSeriesFile(path,checked);
            QString error = probe.error;
            if (error.isEmpty() && (checked.xColumn >= probe.columns.size() || checked.yColumn >= probe.columns.size()))
                error = "Selected column does not exist";
            if (error.isEmpty()) error = readSeriesFile(path,checked,cancellation).error;
            if (error.isEmpty()) valid << path; else skipped << path + " — " + error;
        }
        QMetaObject::invokeMethod(this,[this,rootId,before,identities,options,valid,skipped]() mutable {
            QStringList accepted;
            QMap<QString,QUuid> currentIdentities;
            QMap<QString,QJsonObject> currentOptions;
            for (const auto &source : m_document->sources().value(rootId)) {
                const auto path = source.provenance["path"].toString();
                currentIdentities.insert(path,QUuid(source.provenance["dataset_id"].toString()));
                currentOptions.insert(path,source.provenance["file_options"].toObject());
            }
            for (const auto &path : valid) {
                // A deleted and reimported path is a different dataset, even if
                // its import options still match the validation snapshot.
                if (!identities.value(path).isNull() && currentIdentities.value(path)==identities.value(path) &&
                    currentOptions.value(path)==before.value(path)) accepted<<path;
                else skipped << path + " — " + text("ui.file_options_changed_during_check");
            }
            QScopedValueRollback<bool> committing(m_committingOptions, true);
            if (!accepted.isEmpty() && !m_document->setFileOptions(rootId,accepted,options)) {
                for (const auto &path : accepted) skipped << path + " — " + text("ui.file_options_not_applied");
                accepted.clear();
            }
            // Our own preceding commits are not an external conflict for later
            // queued edits. Keep captured identities and external changes guarded.
            auto advance = [&](OptionsEdit &pending) {
                if (pending.root != rootId) return;
                for (const auto &path : accepted)
                    if (pending.identities.value(path) == identities.value(path) &&
                        pending.before.value(path) == before.value(path)) pending.before[path] = options;
            };
            for (auto &pending : m_optionsQueue) advance(pending);
            advance(m_optionsEdit);
            emit batchOptionsFinished(accepted,skipped);
            if (!skipped.isEmpty()) {
                auto *notice = new QMessageBox(QMessageBox::Information,text("ui.input_settings"),
                    text("ui.file_options_summary").arg(accepted.size()).arg(skipped.size()), QMessageBox::Ok, this);
                notice->setAttribute(Qt::WA_DeleteOnClose);
                notice->setDetailedText(skipped.join("\n")); notice->open();
            }
        },Qt::QueuedConnection);
    });
    auto *worker = m_optionsWorker;
    connect(worker,&QThread::finished,this,[this,worker] {
        if (m_optionsWorker==worker) m_optionsWorker=nullptr;
        worker->deleteLater();
        startOptionsEdit();
        if (!m_optionsWorker && m_optionsQueue.isEmpty()) {
            refresh();
            findChild<QPushButton *>("moveSourceInputs")->setEnabled(!m_optionsWorker);
            findChild<QPushButton *>("removeSourceInput")->setEnabled(!m_optionsWorker);
        }
        emit readinessChanged(readyToRun());
    });
    worker->start();
}
SourceItem SourcePanel::currentSource() const
{
    const auto items=m_document->sources().value(root());
    const int row=m_files->currentRow();
    return row>=0 && row<items.size() ? items[row] : SourceItem{};
}
void SourcePanel::selectDataset(NodeId rootId, QUuid dataset)
{
    selectRoot(rootId);
    const auto items=m_document->sources().value(rootId);
    for (int row=0;row<items.size();++row)
        if (QUuid(items[row].provenance["dataset_id"].toString())==dataset) {
            m_files->setCurrentRow(row,QItemSelectionModel::ClearAndSelect);
            return;
        }
}
NodeId SourcePanel::root() const
{
    return m_roots->currentData().toUuid();
}
void SourcePanel::refresh()
{
    if (m_committingOptions) return;
    m_options->flushEdits();
    const auto previous = root();
    const auto currentPath =
        m_files->currentItem() ? m_files->currentItem()->data(Qt::UserRole).toString() : QString();
    QSet<QString> selected;
    for (auto *item : m_files->selectedItems())
        selected.insert(item->data(Qt::UserRole).toString());
    QSignalBlocker filesBlock(m_files);
    QSignalBlocker selectionBlock(m_files->selectionModel());
    QSignalBlocker block(m_roots);
    const bool updatesEnabled = m_files->updatesEnabled();
    m_files->setUpdatesEnabled(false);
    m_roots->clear();
    int number = 0;
    for (const auto &node : m_document->model().definition().nodes)
    {
        const auto &descriptor = m_document->model().registry().find(node.method)->descriptor;
        if (!node.input && descriptor.requiresSourceBinding)
            m_roots->addItem(methodLabel(descriptor) + " " + QString::number(++number), node.id);
    }
    auto idx = m_roots->findData(previous);
    if (idx >= 0)
        m_roots->setCurrentIndex(idx);
    m_roots->setVisible(m_roots->count() > 1);
    m_files->clear();
    const auto *rootNode = m_document->node(root());
    const bool nonFile = rootNode && rootNode->method != "file_source";
    m_options->setVisible(!nonFile);
    m_selectionHint->setVisible(!nonFile);
    findChild<QToolButton *>("sourceSelectionHelp")->setVisible(!nonFile);
    findChild<QPushButton *>("removeSourceInput")->setVisible(!nonFile);
    findChild<QPushButton *>("moveSourceInputs")->setVisible(!nonFile);
    if (nonFile)
    {
        m_options->setFile({}, {});
        for (const auto &source : m_document->sources().value(root()))
        {
            auto ref = OutputReference::fromJson(source.provenance["database_source"].toObject());
            auto *item = new QListWidgetItem(source.label, m_files);
            item->setData(Qt::UserRole, source.label);
            if (source.label == currentPath) m_files->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
            item->setData(Qt::UserRole + 3, source.label + "\n" + ref.databasePath +
                          "\n" + ref.nodeId.toString() + " / " + ref.port);
            updateFileState(item, m_states.value(root().toString() + "|" + source.label,
                                                 SourceFileState::Pending));
        }
        m_files->setToolTip(text("ui.read_only_reference_a_run_saves_an_independent_input_snapsho"));
        m_count->setText(text("ui.bound_inputs_count").arg(m_files->count()));
        if (!m_files->currentItem() && m_files->count()) m_files->setCurrentRow(0);
        m_files->setUpdatesEnabled(updatesEnabled);
        notifyPreviewSource();
        emit readinessChanged(readyToRun());
        return;
    }
    QItemSelection selection;
    int selectionStart = -1;
    const auto finishSelectionRange = [&](int lastRow)
    {
        if (selectionStart < 0)
            return;
        selection.select(m_files->model()->index(selectionStart, 0),
                         m_files->model()->index(lastRow, 0));
        selectionStart = -1;
    };
    for (const auto &path : m_document->files(root()))
    {
        const auto state = m_states.value(root().toString() + "|" + path, SourceFileState::Pending);
        auto *item = new QListWidgetItem(QFileInfo(path).fileName(), m_files);
        item->setData(Qt::UserRole, path);
        item->setData(Qt::UserRole + 3, path);
        updateFileState(item, state);
        if (path == currentPath)
            m_files->setCurrentItem(item, QItemSelectionModel::NoUpdate);
        const int row = m_files->count() - 1;
        if (selected.contains(path))
        {
            if (selectionStart < 0)
                selectionStart = row;
        }
        else
            finishSelectionRange(row - 1);
    }
    finishSelectionRange(m_files->count() - 1);
    // Restore whole ranges once. Calling setSelected for every inserted item
    // repeatedly merges selections and becomes quadratic for large batches.
    m_files->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    if (!m_files->currentItem() && m_files->count())
        m_files->setCurrentRow(0, selection.isEmpty() ? QItemSelectionModel::ClearAndSelect
                                                   : QItemSelectionModel::NoUpdate);
    m_files->setUpdatesEnabled(updatesEnabled);
    showFileOptions();
    updateSelectionHint();
    m_count->setText(QString::number(m_files->count()) + text("ui.input_files"));
    m_files->setToolTip(
        m_roots->count()
            ? text("ui.drop_files_here_the_workflow_runs_once_per_input")
            : text("ui.importing_creates_a_file_source_generators_run_without_uploa"));
    emit readinessChanged(readyToRun());
}
bool SourcePanel::readyToRun() const
{
    if (m_optionsWorker || !m_optionsQueue.isEmpty()) return false;
    const auto *n = m_document->node(root());
    return !n || n->method != "file_source" || !m_files->currentItem() || m_options->ready();
}
void SourcePanel::selectRoot(NodeId id)
{
    const auto idx = m_roots->findData(id);
    if (idx >= 0)
    {
        if (idx == m_roots->currentIndex())
            syncPreviewInput();
        else
            m_roots->setCurrentIndex(idx);
    }
}
void SourcePanel::addFiles(const QStringList &paths)
{
    const auto result = m_document->importFiles(root(), paths);
    if (!result.ok()) { emit editRejected(result.error); return; }
    if (result.ok() && !result.nodeId.isNull()) selectRoot(result.nodeId);
}
bool SourcePanel::applyCurrentOptions()
{
    m_options->flushEdits();
    if (m_optionsWorker || !m_optionsQueue.isEmpty()) return false;
    const auto *n = m_document->node(root());
    if (!n || n->method != "file_source" || !m_files->currentItem())
        return true;
    if (!m_options->ready())
        return false;
    const auto path = m_files->currentItem()->data(Qt::UserRole).toString();
    const auto options = m_options->options();
    if (options != m_document->fileOptions(root(), path))
        return m_document->setFileOptions(root(), {path}, options);
    return true;
}
void SourcePanel::syncPreviewInput()
{
    const auto *n = m_document->node(root());
    if (n && n->method == "file_source")
        m_options->notifyPreviewInput(); // Also publishes the workflow source.
    else
        notifyPreviewSource();
}
void SourcePanel::notifyPreviewSource()
{
    const auto items = m_document->sources().value(root());
    const int index = m_files->currentRow();
    if (index < 0 || index >= items.size()) { emit previewSourceChanged(root(), {}, false); return; }
    auto source = items[index];
    const auto *n = m_document->node(root());
    const bool file = n && n->method == "file_source";
    emit previewSourceChanged(root(), source, !file || m_options->ready());
}
void SourcePanel::showFileOptions()
{
    m_options->flushEdits();
    m_optionsRoot = root();
    const auto path =
        m_files->currentItem() ? m_files->currentItem()->data(Qt::UserRole).toString() : QString();
    const auto *n = m_document->node(root());
    if (n && n->method == "file_source") m_options->setFile(path, m_document->fileOptions(root(), path));
    updateSelectionHint();
    notifyPreviewSource();
}
void SourcePanel::clearFileStates()
{
    m_states.clear();
    refresh();
}
void SourcePanel::setFileState(NodeId id, const QString &path, SourceFileState state)
{
    m_states[id.toString() + "|" + path] = state;
    if (root() != id)
        return;
    for (int i = 0; i < m_files->count(); ++i)
    {
        auto *item = m_files->item(i);
        if (item->data(Qt::UserRole).toString() == path)
            updateFileState(item, state);
    }
}
void SourcePanel::dragEnterEvent(QDragEnterEvent *e)
{
    if (isEnabled() && e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void SourcePanel::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const auto &url : e->mimeData()->urls())
        if (url.isLocalFile())
            paths.push_back(url.toLocalFile());
    if (!paths.isEmpty())
    {
        addFiles(paths);
        e->acceptProposedAction();
    }
}
} // namespace aster::ui
