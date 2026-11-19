// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
#include "ui/common/wrapping_button.h"
#include "ui/common/file_list_delegate.h"
#include "ui/panels/source_panel.h"
#include "ui/common/presentation.h"
#include "ui/panels/file_options_editor.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
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
#include <utility>
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
FileListStatus sourceStatus(SourceFileState state)
{
    switch (state)
    {
    case SourceFileState::Running: return {"ui.running", "running"};
    case SourceFileState::Saved: return {"ui.complete_saved", "success"};
    case SourceFileState::Unsaved: return {"ui.complete_unsaved", "warning"};
    case SourceFileState::Failed: return {"ui.failed_partial", "error"};
    case SourceFileState::Cancelled: return {"ui.cancelled", "muted", true};
    default: return {"ui.ready", "muted", true};
    }
}
void updateFileState(FileListItem *item, SourceFileState state)
{
    const auto status = sourceStatus(state);
    item->setStatus(status);
    const auto label = text(status.label);
    item->setData(Qt::UserRole + 1, label);
    item->setData(Qt::UserRole + 2, int(state));
    const auto details = item->data(Qt::UserRole + 3).toString();
    item->setData(Qt::AccessibleTextRole, item->text());
    item->setData(Qt::AccessibleDescriptionRole, details + "\n" + label);
}
class SourceFileDelegate final : public FileListDelegate
{
  public:
    using FileListDelegate::FileListDelegate;

  private:
    Row describe(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Row row;
        row.name = index.data(Qt::DisplayRole).toString();
        row.highlighted = option.state.testFlag(QStyle::State_Selected);
        row.current = index == m_view->currentIndex();
        const auto status = sourceStatus(SourceFileState(index.data(Qt::UserRole + 2).toInt()));
        row.dotColor = status.color;
        row.hollowDot = status.hollow;
        return row;
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
    m_files->setMouseTracking(true);
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_files->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_files->setTextElideMode(Qt::ElideMiddle);
    m_files->setMinimumHeight(150);
    filesLayout->addWidget(m_files, 1);
    m_count = new QLabel;
    m_count->setObjectName("Muted");
    // Keep localized file counts from squeezing the action button labels.
    filesLayout->addWidget(m_count);
    auto *fileActions = new QFormLayout;
    // Keep the help button beside Remove. If both actions cannot fit on one
    // row, let the form move the second group below Move instead of clipping.
    fileActions->setRowWrapPolicy(QFormLayout::WrapLongRows);
    fileActions->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    fileActions->setContentsMargins(0, 0, 0, 0);
    fileActions->setSpacing(6);
    auto *remove = new WrappingPushButton(text("ui.remove_short"));
    remove->setToolTip(text("ui.remove_selected_inputs"));
    remove->setObjectName("removeSourceInput");
    remove->setProperty("selectionAction", true);
    auto *move = new WrappingPushButton(text("ui.move_inputs"));
    move->setObjectName("moveSourceInputs");
    move->setProperty("selectionAction", true);
    move->setToolTip(text("ui.move_inputs_hint"));
    connect(move,&QPushButton::clicked,this,&SourcePanel::moveInputsRequested);
    auto *removeActions = new QWidget;
    auto *removeLayout = new QHBoxLayout(removeActions);
    removeLayout->setContentsMargins(0, 0, 0, 0);
    removeLayout->setSpacing(6);
    removeLayout->addWidget(remove, 1);
    auto *help = new QToolButton;
    help->setText("?"); help->setObjectName("sourceSelectionHelp");
    help->setToolTip(text("ui.input_selection_help"));
    help->setAccessibleName(text("ui.input_selection_help"));
    removeLayout->addWidget(help);
    fileActions->addRow(move, removeActions);
    connect(help, &QToolButton::clicked, this, [this] { openHelp(window(), userGuideTopic("input-selection")); });
    filesLayout->addLayout(fileActions);
    m_selectionHint = new QLabel;
    m_selectionHint->setObjectName("sourceSelectionHint");
    m_selectionHint->setWordWrap(true);
    filesLayout->addWidget(m_selectionHint);
    m_optionsToggle = new QToolButton;
    m_optionsToggle->setObjectName("sourceOptionsToggle");
    m_optionsToggle->setText(text("ui.input_settings"));
    m_optionsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_optionsToggle->setArrowType(Qt::RightArrow);
    m_optionsToggle->setCheckable(true);
    filesLayout->addWidget(m_optionsToggle, 0, Qt::AlignLeft);
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
    splitter->setStretchFactor(1, 1);
    connect(m_optionsToggle, &QToolButton::toggled, this,
            [this, splitter, previousHeight = 0](bool expanded) mutable {
        if (!expanded) previousHeight = splitter->sizes().value(1);
        m_optionsToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_options->setVisible(expanded);
        if (expanded) {
            const int available = splitter->height() - splitter->handleWidth();
            const int height = previousHeight > 0 ? previousHeight : available / 3;
            splitter->setSizes({qMax(0, available - height), height});
        }
    });
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
                if (m_beforeInputRemoval && !m_beforeInputRemoval()) return;
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
    connect(m_document, &WorkflowDocument::sourcesChanged, this, [this] {
        m_statusDatasets.clear();
        refresh();
    });
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
void SourcePanel::updateRunControls()
{
    const bool editable = !m_document->editingLocked();
    findChild<QPushButton *>("importFiles")->setEnabled(editable);
    findChild<QPushButton *>("moveSourceInputs")->setEnabled(editable && !m_optionsWorker);
    findChild<QPushButton *>("removeSourceInput")->setEnabled(editable && !m_optionsWorker);
    m_optionsToggle->setEnabled(editable);
    m_options->setVisible(editable && !m_optionsToggle->isHidden() && m_optionsToggle->isChecked());
    setAcceptDrops(editable);
    updateSelectionHint();
}

void SourcePanel::updateSelectionHint()
{
    m_selectionHint->setText(text("ui.input_selection_scope").arg(m_files->selectedItems().size()));
    m_options->setEnabled(!m_document->editingLocked() && m_files->currentItem() && m_files->currentItem()->isSelected());
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
    const auto currentDataset = m_files->currentItem()
        ? m_files->currentItem()->data(Qt::UserRole + 4).toUuid() : QUuid{};
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
    m_visibleFiles.clear();
    m_files->clear();
    const auto *rootNode = m_document->node(root());
    const bool nonFile = rootNode && rootNode->method != "file_source";
    m_optionsToggle->setVisible(!nonFile);
    m_options->setVisible(!nonFile && m_optionsToggle->isChecked() && !m_document->editingLocked());
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
            const auto dataset = QUuid(source.provenance["dataset_id"].toString());
            const auto state = m_states.value({root(), dataset}, SourceFileState::Pending);
            const auto location = ref.valid() ? ref.databasePath + "\n" + ref.nodeId.toString() + " / " + ref.port : ref.databasePath;
            auto *item = new FileListItem(source.label, m_files, {location, {}, {}}, sourceStatus(state));
            m_visibleFiles.insert(source.label, item);
            item->setData(Qt::UserRole, source.label);
            item->setData(Qt::UserRole + 4, dataset);
            if (!currentDataset.isNull() && dataset == currentDataset)
                m_files->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
            item->setData(Qt::UserRole + 3, location);
            updateFileState(item, state);
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
    const auto rootId = root();
    for (const auto &source : m_document->sources().value(rootId))
    {
        const auto path = source.provenance["path"].toString();
        const QUuid dataset(source.provenance["dataset_id"].toString());
        const auto state = m_states.value({rootId, dataset}, SourceFileState::Pending);
        auto *item = new FileListItem(QFileInfo(path).fileName(), m_files, {path, {}, {}}, sourceStatus(state));
        m_visibleFiles.insert(path, item);
        item->setData(Qt::UserRole, path);
        item->setData(Qt::UserRole + 4, dataset);
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
    if (m_document->editingLocked()) return; // Browsing status starts no file probe.
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
    for (auto *item : std::as_const(m_visibleFiles))
        if (item->data(Qt::UserRole + 2).toInt() != int(SourceFileState::Pending))
            updateFileState(item, SourceFileState::Pending);
}
void SourcePanel::setFileState(NodeId id, const QString &path, SourceFileState state)
{
    // Batch events carry source labels. Resolve them once per tree and retain the
    // dataset identity so removing and importing the same path starts a new run history.
    if (!m_statusDatasets.contains(id)) {
        auto &byLabel = m_statusDatasets[id];
        for (const auto &source : m_document->sources().value(id)) {
            const QUuid dataset(source.provenance["dataset_id"].toString());
            if (!dataset.isNull()) byLabel.insert(source.label, dataset);
        }
    }
    const auto tree = m_statusDatasets.constFind(id);
    const auto range = tree.value().equal_range(path);
    bool changed = false;
    for (auto it = range.first; it != range.second; ++it) {
        const auto key = qMakePair(id, it.value());
        if (m_states.value(key, SourceFileState::Pending) != state) {
            m_states.insert(key, state);
            changed = true;
        }
    }
    if (!changed || root() != id) return;
    const auto matches = m_visibleFiles.equal_range(path);
    for (auto it = matches.first; it != matches.second; ++it)
        updateFileState(it.value(), state);
}
void SourcePanel::dragEnterEvent(QDragEnterEvent *e)
{
    if (isEnabled() && e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void SourcePanel::dropEvent(QDropEvent *e)
{
    if (m_document->editingLocked()) return;
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
