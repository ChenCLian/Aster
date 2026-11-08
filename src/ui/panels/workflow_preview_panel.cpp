// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
#include "ui/panels/workflow_preview_panel.h"
#include "ui/panels/file_preview_panel.h"
#include "ui/panels/style_section.h"
#include "ui/results/plot_view.h"
#include "ui/common/presentation.h"
#include "ui/common/error_presenter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "ui/common/section_splitter.h"
#include <QLabel>
#include <QComboBox>
#include <QProgressBar>
#include <QToolButton>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QShowEvent>
#include <QHideEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QStyleOption>
namespace aster::ui
{
namespace
{
// Two explicit lines, never a path-dependent paragraph. QLabel retains the
// full accessible text; painting elides each line and the tooltip holds detail.
class PreviewSourceHeading final : public QLabel
{
  public:
    PreviewSourceHeading() { setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); }
    QSize sizeHint() const override { return {0, 2 * fontMetrics().lineSpacing() + 2}; }
    QSize minimumSizeHint() const override { return sizeHint(); }
  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QStyleOption option; option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
        const auto lines = text().split('\n');
        auto row = contentsRect(); row.setHeight(fontMetrics().lineSpacing());
        for (int i = 0; i < std::min(2, int(lines.size())); ++i)
        {
            style()->drawItemText(&painter, row, Qt::AlignLeft | Qt::AlignVCenter,
                palette(), isEnabled(), fontMetrics().elidedText(lines[i], Qt::ElideMiddle, row.width()), foregroundRole());
            row.translate(0, row.height());
        }
    }
};
void compactPreview(PlotView *view)
{
    if (!view->hasPlot()) return;
    auto style = view->style();
    style.axisFontSize = 8; style.legendFontSize = 7.5;
    style.x.tickCount = style.y.tickCount = 4;
    view->applyStyle(style);
}
}

WorkflowPreviewPanel::WorkflowPreviewPanel(WorkflowDocument *doc, QWidget *parent)
    : QWidget(parent), m_document(doc), m_controller(new WorkflowPreviewController(doc, this))
{
    setObjectName("workflowPreview");
    setMinimumWidth(0);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8); layout->setSpacing(8);
    auto *split = new SectionSplitter(Qt::Vertical);
    split->setObjectName("previewPlotsSplitter"); split->setChildrenCollapsible(false);
    auto *top = new QWidget;
    top->setMinimumHeight(160);
    auto *tl = new QVBoxLayout(top); tl->setContentsMargins(0, 0, 0, 0);
    m_original = new FilePreviewPanel;
    tl->addWidget(m_original);
    m_sourceHeading = new PreviewSourceHeading; m_sourceHeading->setObjectName("PreviewMethod");
    m_sourceHeading->setWordWrap(true); m_sourceHeading->setTextFormat(Qt::PlainText);
    m_sourceHeading->setMinimumWidth(0); m_sourceHeading->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    tl->addWidget(m_sourceHeading); m_sourceHeading->hide();
    m_pairInput = new ChoiceEditor; m_pairInput->setObjectName("pairInputPreview");
    m_pairInput->addItems({"A", "B"}); tl->addWidget(m_pairInput); m_pairInput->hide();
    connect(m_pairInput, qOverload<int>(&QComboBox::currentIndexChanged), this, &WorkflowPreviewPanel::showSourceOutput);
    m_generated = new PlotView; m_generated->setObjectName("sourcePreviewPlot");
    m_generated->setMinimumSize(0, 150);
    tl->addWidget(m_generated, 1); m_generated->hide();
    split->addWidget(top);
    auto *bottom = new QWidget; bottom->setMinimumHeight(230);
    auto *bl = new QVBoxLayout(bottom); bl->setContentsMargins(0, 0, 0, 0); bl->setSpacing(6);
    auto *bar = new QFrame; bar->setObjectName("PreviewIdentity");
    auto *info = new QVBoxLayout(bar); info->setContentsMargins(8, 10, 8, 10); info->setSpacing(4);
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
    m_tools = new QBoxLayout(QBoxLayout::LeftToRight);
    m_outputs = new ChoiceEditor; m_outputs->setObjectName("previewOutput");
    m_outputs->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_outputs->setMinimumContentsLength(6); m_outputs->setMinimumWidth(0);
    m_outputs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_outputs->setToolTip(text("ui.preview_outputs_hint")); m_tools->addWidget(m_outputs, 1);
    m_actions = new QWidget;
    auto *actions = new QHBoxLayout(m_actions);
    actions->setContentsMargins(0, 0, 0, 0);
    m_fit = new QToolButton; m_fit->setText(text("ui.fit")); actions->addWidget(m_fit);
    m_legend = new QToolButton; m_legend->setText(text("ui.preview_legend"));
    m_legend->setCheckable(true); actions->addWidget(m_legend);
    m_retry = new QToolButton; m_retry->setText(text("ui.preview_refresh")); actions->addWidget(m_retry);
    m_tools->addWidget(m_actions);
    bl->addLayout(m_tools);
    m_progress = new QProgressBar; m_progress->setRange(0, 0); m_progress->setTextVisible(false);
    m_progress->setFixedHeight(4); bl->addWidget(m_progress);
    m_status = new StyleNote; m_status->setWordWrap(true); m_status->setObjectName("Muted");
    m_status->setTextFormat(Qt::PlainText); m_status->setMinimumWidth(0);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); bl->addWidget(m_status);
    m_resultPlot = new PlotView; m_resultPlot->setObjectName("nodePreviewPlot");
    m_resultPlot->setMinimumSize(0, 0); bl->addWidget(m_resultPlot, 1);
    connect(m_resultPlot, &PlotView::outputChanged, this, [this] {
        m_fit->setEnabled(m_resultPlot->hasPlot()); m_legend->setEnabled(m_resultPlot->hasPlot());
    });
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
void WorkflowPreviewPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Keep the output choice usable when the sidebar is narrow or translated
    // buttons are long. Reuse the same controls so focus/selection is preserved.
    const bool stacked = width() - 16 < m_actions->minimumSizeHint().width() + 120 + m_tools->spacing();
    m_tools->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_tools->setStretch(0, stacked ? 0 : 1);
}
void WorkflowPreviewPanel::updateState()
{
    for (auto it = m_outputSelections.begin(); it != m_outputSelections.end();)
        if (!m_document->node(it.key())) it = m_outputSelections.erase(it);
        else ++it;
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
    const bool paired = !m_document->pairNodeFor(m_controller->node()).isNull();
    m_pairInput->setVisible(paired);
    const bool file = !paired && (!root || root->method == "file_source");
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
    if (paired && current) m_file->setText(current->pairLabel);
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
        showSourceOutput();
        return;
    }
    if (m_displayed == current) return;
    const auto previous = m_outputSelections.value(m_controller->node());
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
    m_outputSelections.insert(m_controller->node(), m_outputs->currentData().toString());
    m_resultPlot->showOutput(m_displayed->output, m_outputs->currentData().toString());
    auto style = m_resultPlot->style();
    style.axisFontSize = 8; style.legendFontSize = 7.5; style.x.tickCount = style.y.tickCount = 4;
    style.legend = m_legend->isChecked();
    m_resultPlot->applyStyle(style);
    m_fit->setEnabled(m_resultPlot->hasPlot()); m_legend->setEnabled(m_resultPlot->hasPlot());
    showSourceOutput();
}
void WorkflowPreviewPanel::showSourceOutput()
{
    const auto *root = m_document->node(m_controller->node());
    const auto currentPair = m_controller->result();
    if (!m_document->pairNodeFor(m_controller->node()).isNull())
    {
        const bool b = m_pairInput->currentIndex() == 1;
        m_sourceHeading->setText(text("ui.pair_input") + (b ? " B" : " A") +
            (currentPair ? "\n" + (b ? currentPair->displayB : currentPair->displayA) : QString()));
        m_sourceHeading->setToolTip(currentPair ? (b ? currentPair->labelB : currentPair->labelA) : QString());
        const auto input = currentPair ? (b ? currentPair->second : currentPair->original) : NodeResult{};
        if (input.state == NodeState::Succeeded && input.outputs.value("series"))
        {
            m_generated->showOutput(input, "series");
            compactPreview(m_generated);
        }
        else m_generated->clear(text("ui.preview_independent_source"));
        return;
    }
    QString port = m_outputs->currentData().toString();
    // Follow the selected branch back to its first edge. A grandchild's own
    // input port belongs to its immediate parent, not necessarily the source.
    while (root && root->input)
    {
        port = root->input->output;
        root = m_document->node(root->input->parent);
    }
    if (!root || root->method == "file_source")
    {
        m_generated->clear(text("ui.preview_independent_source"));
        return;
    }
    const auto *entry = m_document->model().registry().find(root->method);
    if (!entry) return;
    auto heading = text("ui.preview_source_data") + " · " + methodLabel(entry->descriptor);
    if (entry->descriptor.outputs.size() > 1 && !port.isEmpty())
        heading += "\n" + portLabel(entry->descriptor, port);
    m_sourceHeading->setText(heading);
    m_sourceHeading->setToolTip(heading);
    const auto current = m_controller->result();
    if (!current || current->original.state != NodeState::Succeeded ||
        !current->original.outputs.contains(port))
    {
        m_generated->clear(text("ui.preview_independent_source"));
        return;
    }
    // Keep a successful source visible when only a downstream node fails.
    // Reusing the same data/port also preserves source-axis zoom while the
    // user switches the selected analysis node's output for display.
    if (m_generated->currentData() != current->original.outputs.value(port) ||
        m_generated->currentPort() != port)
    {
        m_generated->showOutput(current->original, port);
        compactPreview(m_generated);
    }
}
}
