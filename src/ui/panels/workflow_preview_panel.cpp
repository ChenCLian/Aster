#include "ui/panels/workflow_preview_panel.h"
#include "ui/panels/file_preview_panel.h"
#include "ui/panels/style_section.h"
#include "ui/results/plot_view.h"
#include "ui/common/presentation.h"
#include "ui/common/error_presenter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QComboBox>
#include <QProgressBar>
#include <QToolButton>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QShowEvent>
#include <QHideEvent>
namespace aster::ui
{
WorkflowPreviewPanel::WorkflowPreviewPanel(WorkflowDocument *doc, QWidget *parent)
    : QWidget(parent), m_document(doc), m_controller(new WorkflowPreviewController(doc, this))
{
    setObjectName("workflowPreview");
    setMinimumWidth(0);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 6); layout->setSpacing(6);
    auto *split = new QSplitter(Qt::Vertical);
    split->setObjectName("previewPlotsSplitter"); split->setChildrenCollapsible(false);
    auto *top = new QWidget;
    top->setMinimumHeight(160);
    auto *tl = new QVBoxLayout(top); tl->setContentsMargins(0, 0, 0, 0);
    m_original = new FilePreviewPanel;
    tl->addWidget(m_original);
    m_sourceHeading = new QLabel; m_sourceHeading->setObjectName("PreviewMethod");
    m_sourceHeading->setWordWrap(true); m_sourceHeading->setTextFormat(Qt::PlainText);
    m_sourceHeading->setMinimumWidth(0); m_sourceHeading->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    tl->addWidget(m_sourceHeading); m_sourceHeading->hide();
    m_generated = new PlotView; m_generated->setMinimumSize(0, 0);
    tl->addWidget(m_generated); m_generated->hide();
    split->addWidget(top);
    auto *bottom = new QWidget; bottom->setMinimumHeight(230);
    auto *bl = new QVBoxLayout(bottom); bl->setContentsMargins(0, 0, 0, 0); bl->setSpacing(6);
    auto *bar = new QFrame; bar->setObjectName("PreviewIdentity");
    auto *info = new QVBoxLayout(bar); info->setContentsMargins(10, 9, 10, 9); info->setSpacing(4);
    auto *caption = new QLabel(text("ui.preview_result")); caption->setObjectName("Muted");
    info->addWidget(caption);
    m_method = new QLabel; m_method->setObjectName("PreviewMethod");
    m_method->setTextFormat(Qt::PlainText); m_method->setWordWrap(true);
    m_method->setMinimumWidth(0); m_method->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    info->addWidget(m_method);
    m_file = new QLabel; m_file->setTextFormat(Qt::PlainText); m_file->setWordWrap(true);
    m_file->setObjectName("previewInputIdentity"); m_file->setMinimumWidth(0);
    m_file->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); info->addWidget(m_file);
    bl->addWidget(bar);
    auto *tools = new QHBoxLayout;
    m_outputs = new QComboBox; m_outputs->setObjectName("previewOutput");
    m_outputs->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_outputs->setMinimumContentsLength(6); m_outputs->setMinimumWidth(0);
    m_outputs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_outputs->setToolTip(text("ui.preview_outputs_hint")); tools->addWidget(m_outputs, 1);
    m_fit = new QToolButton; m_fit->setText(text("ui.fit")); tools->addWidget(m_fit);
    m_legend = new QToolButton; m_legend->setText(text("ui.preview_legend"));
    m_legend->setCheckable(true); tools->addWidget(m_legend);
    m_retry = new QToolButton; m_retry->setText(text("ui.preview_refresh")); tools->addWidget(m_retry);
    bl->addLayout(tools);
    m_progress = new QProgressBar; m_progress->setRange(0, 0); m_progress->setTextVisible(false);
    m_progress->setFixedHeight(4); bl->addWidget(m_progress);
    m_status = new StyleNote; m_status->setWordWrap(true); m_status->setObjectName("Muted");
    m_status->setTextFormat(Qt::PlainText); m_status->setMinimumWidth(0);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); bl->addWidget(m_status);
    m_resultPlot = new PlotView; m_resultPlot->setObjectName("nodePreviewPlot");
    m_resultPlot->setMinimumSize(0, 0); bl->addWidget(m_resultPlot, 1);
    m_outputHint = new StyleNote(text("ui.preview_reduced")); m_outputHint->setWordWrap(true);
    m_outputHint->setObjectName("Muted"); m_outputHint->setMinimumWidth(0);
    m_outputHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); bl->addWidget(m_outputHint);
    split->addWidget(bottom); split->setStretchFactor(0, 2); split->setStretchFactor(1, 3);
    split->setSizes({260, 400}); layout->addWidget(split, 1);
    connect(m_controller, &WorkflowPreviewController::changed, this, &WorkflowPreviewPanel::updateState);
    connect(m_outputs, qOverload<int>(&QComboBox::currentIndexChanged), this, &WorkflowPreviewPanel::showOutput);
    connect(m_fit, &QToolButton::clicked, m_resultPlot, &PlotView::fit);
    connect(m_legend, &QToolButton::toggled, m_resultPlot, &PlotView::setLegend);
    connect(m_retry, &QToolButton::clicked, m_controller, &WorkflowPreviewController::retry);
    updateState();
}
void WorkflowPreviewPanel::setFile(const QString &path, const QJsonObject &options, bool ready)
{ m_original->setInput(path, options, ready); }
void WorkflowPreviewPanel::setSuspended(bool suspended)
{ m_suspended = suspended; m_controller->setActive(isVisible() && !suspended); }
void WorkflowPreviewPanel::showEvent(QShowEvent *event)
{ QWidget::showEvent(event); m_controller->setActive(!m_suspended); }
void WorkflowPreviewPanel::hideEvent(QHideEvent *event)
{ m_controller->setActive(false); QWidget::hideEvent(event); }
void WorkflowPreviewPanel::updateState()
{
    using State = WorkflowPreviewController::State;
    const auto state = m_controller->state();
    const auto *node = m_document->node(m_controller->node());
    const auto *entry = node ? m_document->model().registry().find(node->method) : nullptr;
    m_method->setText(entry ? methodLabel(entry->descriptor) : text("ui.preview_choose_node"));
    m_method->parentWidget()->setVisible(entry != nullptr);
    auto *root = node;
    QStringList names;
    while (root)
    {
        const auto *r = m_document->model().registry().find(root->method);
        if (r) names.prepend(methodLabel(r->descriptor));
        if (!root->input) break;
        root = m_document->node(root->input->parent);
    }
    const bool file = !root || root->method == "file_source";
    m_original->setVisible(file); m_generated->setVisible(!file);
    m_sourceHeading->setVisible(!file);
    m_sourceHeading->setText(text("ui.preview_source_data") + " · " + names.value(0));
    const auto label = m_controller->sourceLabel();
    m_file->setText(root && !file ? names.value(0) : QFileInfo(label).fileName());
    m_file->setToolTip((file ? label : names.value(0)) + "\n" + names.join(" → "));
    m_method->setToolTip(names.join(" → "));
    m_progress->setVisible(state == State::Running || state == State::Waiting);
    m_retry->setEnabled(node && state != State::Running && state != State::Waiting);
    const auto current = m_controller->result();
    if (file && current && current != m_displayed) m_original->setWorkflowSource(current->original);
    if (state != State::Ready || !current)
    {
        m_outputHint->hide();
        m_displayed.reset();
        QSignalBlocker block(m_outputs); m_outputs->clear(); m_outputs->setEnabled(false);
        m_fit->setEnabled(false); m_legend->setEnabled(false);
        QString message = text(state == State::Running ? "ui.preview_running" :
                               state == State::Waiting ? "ui.preview_waiting" :
                               state == State::Paused ? "ui.preview_paused" : "ui.preview_idle");
        if (!m_controller->message().isEmpty()) message = m_controller->message();
        QString details;
        if (state == State::Failed && current)
        {
            const auto error = presentError(current->output);
            message = error.summary + (error.suggestion.isEmpty() ? QString() : "\n" + error.suggestion);
            details = error.details;
            const auto *failed = m_document->node(current->failedNode);
            if (failed)
                message = methodLabel(m_document->model().registry().find(failed->method)->descriptor) + " · " + message;
        }
        m_status->setText(message); m_status->setToolTip(details);
        m_resultPlot->clear(text("ui.preview_no_result"));
        if (!file) m_generated->clear(text("ui.preview_independent_source"));
        return;
    }
    if (m_displayed == current) return;
    const auto previous = m_outputs->currentData().toString();
    m_displayed = current;
    QSignalBlocker block(m_outputs); m_outputs->clear();
    if (entry)
        for (const auto &output : entry->descriptor.outputs)
            if (current->output.outputs.contains(output.key))
                m_outputs->addItem(portLabel(entry->descriptor, output.key), output.key);
    int index = m_outputs->findData(previous);
    if (index < 0 && entry) index = m_outputs->findData(entry->descriptor.defaultOutput);
    m_outputs->setCurrentIndex(index >= 0 ? index : 0);
    m_outputs->setEnabled(m_outputs->count() > 1);
    if (!file && root)
    {
        const auto &d = m_document->model().registry().find(root->method)->descriptor;
        m_generated->showOutput(current->original, d.defaultOutput);
    }
    QStringList notes;
    for (const auto &warning : current->output.warnings)
        notes << translatedLabel(warning, warning);
    m_status->setToolTip(notes.join("\n\n"));
    auto completed = text("ui.preview_complete").arg(current->executed.size());
    if (!notes.isEmpty())
        completed += text("ui.preview_warning_count").arg(notes.size());
    m_status->setText(completed);
    m_outputHint->setVisible(current->output.report["display_reduced"].toBool());
    showOutput();
}
void WorkflowPreviewPanel::showOutput()
{
    if (!m_displayed || m_outputs->currentIndex() < 0) return;
    m_resultPlot->showOutput(m_displayed->output, m_outputs->currentData().toString());
    auto style = m_resultPlot->style();
    style.axisFontSize = 8; style.legendFontSize = 7.5; style.x.tickCount = style.y.tickCount = 4;
    style.legend = m_legend->isChecked();
    m_resultPlot->applyStyle(style);
    m_fit->setEnabled(m_resultPlot->hasPlot()); m_legend->setEnabled(m_resultPlot->hasPlot());
}
}
