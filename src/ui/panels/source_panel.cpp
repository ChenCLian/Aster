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
#include <QSplitter>
namespace aster::ui
{
namespace
{
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
        const auto rect = opt.rect.adjusted(8, 5, -8, -4);
        const int height = opt.fontMetrics.height();
        painter->setPen(themeColor("text"));
        painter->drawText(QRect(rect.x(), rect.y(), rect.width(), height),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          opt.fontMetrics.elidedText(name, Qt::ElideMiddle, rect.width()));
        painter->setPen(themeColor("muted"));
        painter->drawText(QRect(rect.x(), rect.y() + height + 2, rect.width(), height),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          opt.fontMetrics.elidedText(index.data(Qt::UserRole + 1).toString(),
                                                     Qt::ElideRight, rect.width()));
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return {100, option.fontMetrics.height() * 2 + 12};
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
    m_roots = new QComboBox;
    m_roots->setObjectName("sourceRoot");
    m_roots->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_roots->setMinimumContentsLength(6);
    m_roots->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(m_roots);
    connect(add, &QPushButton::clicked, this, &SourcePanel::importRequested);
    // Keep the file list and its options independently resizable.
    auto *splitter = new QSplitter(Qt::Vertical);
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
    auto *fileActions = new QHBoxLayout;
    fileActions->addWidget(m_count, 1);
    auto *remove = new QPushButton(text("ui.remove_short"));
    remove->setToolTip(text("ui.remove_selected_inputs"));
    remove->setObjectName("removeSourceInput");
    fileActions->addWidget(remove);
    filesLayout->addLayout(fileActions);
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
    connect(m_options, &FileOptionsEditor::applyRequested, this,
            [this](const QJsonObject &options, bool selection)
            {
                QStringList paths;
                if (selection)
                {
                    for (auto *item : m_files->selectedItems())
                        paths << item->data(Qt::UserRole).toString();
                }
                else if (m_files->currentItem())
                    paths << m_files->currentItem()->data(Qt::UserRole).toString();
                m_document->setFileOptions(root(), paths, options);
            });
    connect(remove, &QPushButton::clicked, this,
            [this]
            {
                auto paths = m_document->files(root());
                for (auto *item : m_files->selectedItems())
                    paths.removeAll(item->data(Qt::UserRole).toString());
                m_document->setFiles(root(), paths);
            });
    connect(m_roots, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refresh(); });
    connect(m_document, &WorkflowDocument::changed, this, &SourcePanel::refresh);
    connect(m_document, &WorkflowDocument::sourcesChanged, this, &SourcePanel::refresh);
    refresh();
}
NodeId SourcePanel::root() const
{
    return m_roots->currentData().toUuid();
}
void SourcePanel::refresh()
{
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
    findChild<QPushButton *>("removeSourceInput")->setVisible(!nonFile);
    if (nonFile)
    {
        m_options->setFile({}, {});
        for (const auto &source : m_document->sources().value(root()))
        {
            auto ref = OutputReference::fromJson(source.provenance["database_source"].toObject());
            auto *item = new QListWidgetItem(source.label, m_files);
            item->setData(Qt::UserRole, source.label);
            if (source.label == currentPath) m_files->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
            item->setData(Qt::UserRole + 1, m_states.value(root().toString() + "|" + source.label,
                                                           text("ui.ready")));
            item->setToolTip(ref.databasePath + "\n" + ref.nodeId.toString() + " / " + ref.port);
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
        const auto state = m_states.value(root().toString() + "|" + path, text("ui.ready"));
        auto *item = new QListWidgetItem(QFileInfo(path).fileName(), m_files);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        item->setData(Qt::UserRole + 1, state);
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
    m_count->setText(QString::number(m_files->count()) + text("ui.input_files"));
    m_files->setToolTip(
        m_roots->count()
            ? text("ui.drop_files_here_the_workflow_runs_once_per_input")
            : text("ui.importing_creates_a_file_source_generators_run_without_uploa"));
    emit readinessChanged(readyToRun());
}
bool SourcePanel::readyToRun() const
{
    const auto *n = m_document->node(root());
    return !n || n->method != "file_source" || !m_files->currentItem() || m_options->ready();
}
void SourcePanel::selectRoot(NodeId id)
{
    const auto idx = m_roots->findData(id);
    if (idx >= 0)
        m_roots->setCurrentIndex(idx);
}
void SourcePanel::addFiles(const QStringList &paths)
{
    auto id = root();
    if (!m_document->node(id) || m_document->node(id)->method != "file_source")
    {
        auto result = m_document->addRoot("file_source");
        if (!result.ok())
            return;
        id = result.nodeId;
        selectRoot(id);
    }
    auto list = m_document->files(id);
    list.append(paths);
    m_document->setFiles(id, list);
}
bool SourcePanel::applyCurrentOptions()
{
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
void SourcePanel::notifyPreviewSource()
{
    const auto items = m_document->sources().value(root());
    const int index = m_files->currentRow();
    if (index < 0 || index >= items.size()) { emit previewSourceChanged(root(), {}, false); return; }
    auto source = items[index];
    const auto *n = m_document->node(root());
    const bool file = n && n->method == "file_source";
    if (file) source.provenance["file_options"] = m_options->options();
    emit previewSourceChanged(root(), source, !file || m_options->ready());
}
void SourcePanel::showFileOptions()
{
    const auto path =
        m_files->currentItem() ? m_files->currentItem()->data(Qt::UserRole).toString() : QString();
    const auto *n = m_document->node(root());
    if (n && n->method == "file_source") m_options->setFile(path, m_document->fileOptions(root(), path));
    notifyPreviewSource();
}
void SourcePanel::clearFileStates()
{
    m_states.clear();
    refresh();
}
void SourcePanel::setFileState(NodeId id, const QString &path, const QString &state)
{
    m_states[id.toString() + "|" + path] = state;
    if (root() != id)
        return;
    for (int i = 0; i < m_files->count(); ++i)
    {
        auto *item = m_files->item(i);
        if (item->data(Qt::UserRole).toString() == path)
            item->setData(Qt::UserRole + 1, state);
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
