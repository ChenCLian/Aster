// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
#include "ui/common/numeric_editor.h"
#include "ui/common/segmented_selector.h"
#include "ui/panels/color_button.h"
#include "ui/panels/canvas_details_editor.h"
#include "ui/panels/heatmap_style_editor.h"
#include "ui/panels/style_section.h"
#include "ui/settings/theme_manager.h"
#include "ui/panels/plot_style_editor.h"
#include "ui/panels/axis_details_editor.h"
#include "ui/results/plot_view.h"
#include "ui/common/presentation.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QScrollArea>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QScopedValueRollback>
#include <QDoubleValidator>
#include <QLabel>
#include <QLocale>
#include <limits>
namespace aster::ui
{
namespace
{
QScrollArea *verticalScroller(QWidget *widget)
{
    auto *area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setFrameShape(QFrame::NoFrame);
    widget->setMinimumWidth(0);
    widget->setObjectName("Surface");
    area->setWidget(widget);
    return area;
}
QComboBox *combo(const QString &name)
{
    auto *c = new ChoiceEditor;
    c->setObjectName(name);
    c->setMinimumWidth(0);
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(4);
    c->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    return c;
}
QLineEdit *number(const QString &name)
{
    auto *edit = new QLineEdit;
    edit->setObjectName(name);
    edit->setMinimumWidth(0);
    auto *validator = new QDoubleValidator(edit);
    validator->setLocale(QLocale::c());
    validator->setNotation(QDoubleValidator::ScientificNotation);
    edit->setValidator(validator);
    edit->setPlaceholderText(text("ui.scientific_notation_accepted"));
    return edit;
}
QString displayedNumber(double v)
{
    return QString::number(v, 'g', 12);
}
double value(QLineEdit *edit)
{
    bool ok = false;
    double v = QLocale::c().toDouble(edit->text(), &ok);
    return ok ? v : std::numeric_limits<double>::quiet_NaN();
}
} // namespace
PlotStyleEditor::PlotStyleEditor(PlotView *view, QWidget *parent) : QWidget(parent)
{
    setObjectName("plotStyleEditor");
    setMinimumWidth(0);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(8);
    m_tabs = new QTabWidget;
    auto *tabs = m_tabs;
    tabs->setObjectName("plotStyleSections");
    tabs->setDocumentMode(true);
    tabs->tabBar()->setObjectName("inspectorSectionNav");
    tabs->tabBar()->setExpanding(true);
    tabs->tabBar()->setUsesScrollButtons(false);
    m_canvasDetails = new CanvasDetailsEditor;
    tabs->addTab(verticalScroller(m_canvasDetails), text("ui.canvas"));
    auto *axes = new QWidget;
    auto *al = styleSections(axes);
    auto *axesPage = new QWidget;
    auto *axesPageLayout = new QVBoxLayout(axesPage);
    axesPageLayout->setContentsMargins(0, 8, 0, 0);
    axesPageLayout->setSpacing(6);
    m_axis = new SegmentedSelector;
    m_axis->setObjectName("styleAxis");
    m_axis->setAccessibleName(text("ui.axis_65a010"));
    m_axis->addTab(text("ui.x_axis"));
    m_axis->addTab(text("ui.y_axis"));
    auto *axisHeader = new QWidget;
    auto *axisHeaderLayout = new QHBoxLayout(axisHeader);
    axisHeaderLayout->setContentsMargins(8, 0, 8, 0);
    axisHeaderLayout->addWidget(m_axis);
    axesPageLayout->addWidget(axisHeader);
    auto *af = new StyleSection(text("ui.style_axis_range"));
    al->addWidget(af);
    m_axisLabel = new QLineEdit;
    m_axisLabel->setObjectName("axisLabel");
    af->addRow(text("ui.axis_title"), m_axisLabel);
    m_log = new QCheckBox(text("ui.logarithmic_scale"));
    m_log->setObjectName("axisLog");
    m_reverse = new QCheckBox(text("ui.reverse_direction"));
    m_reverse->setObjectName("axisReverse");
    m_grid = new QCheckBox(text("ui.show_grid"));
    m_grid->setObjectName("axisGrid");
    m_manual = new QCheckBox(text("ui.set_display_limits"));
    m_manual->setObjectName("axisManual");
    af->addRow(m_log);
    af->addRow(m_reverse);
    af->addRow(m_grid);
    af->addRow(m_manual);
    m_lower = number("axisLower");
    m_upper = number("axisUpper");
    af->addRow(text("ui.lower_original_coordinates"), m_lower);
    af->addRow(text("ui.upper_original_coordinates"), m_upper);
    auto *capture = new QPushButton(text("ui.use_current_view_limits"));
    capture->setObjectName("capturePlotRange");
    af->addRow(capture);
    auto *logNote =
        new StyleNote(text("ui.log_scale_hides_non_positive_values_without_altering_data_en"));
    logNote->setWordWrap(true);
    logNote->setObjectName("Muted");
    af->addRow(logNote);
    m_axisDetails = new AxisDetailsEditor;
    // The enclosing axes page already supplies the section gutters.
    m_axisDetails->layout()->setContentsMargins(0, 0, 0, 0);
    al->addWidget(m_axisDetails);
    axesPageLayout->addWidget(verticalScroller(axes), 1);
    tabs->addTab(axesPage, text("ui.axes"));
    auto *curves = new QWidget;
    auto *cr = new QVBoxLayout(curves);
    cr->setContentsMargins(0, 0, 0, 0);
    m_curve = combo("styleCurve");
    cr->addWidget(m_curve);
    m_curveFields = new QWidget;
    auto *cl = styleSections(m_curveFields);
    StyleSection *pf = nullptr;
    auto section = [&](const QString &label) { pf = new StyleSection(label); cl->addWidget(pf); };
    section(text("ui.style_curve_identity"));
    m_visible = new QCheckBox(text("ui.visible"));
    m_visible->setObjectName("curveVisible");
    pf->addRow(m_visible);
    m_curveName = new QLineEdit;
    m_curveName->setObjectName("curveName");
    pf->addRow(text("ui.legend_label"), m_curveName);
    m_inLegend = new QCheckBox(text("ui.include_in_legend"));
    pf->addRow(m_inLegend);
    section(text("ui.style_curve_line"));
    m_color = new ColorButton;
    pf->addRow(text("ui.color_1d0c83"), m_color);
    m_width = new DecimalEditor;
    m_width->setRange(.25, 10);
    m_width->setDecimals(2);
    m_width->setSingleStep(.25);
    m_width->setObjectName("curveWidth");
    pf->addRow(text("ui.line_width"), m_width);
    m_line = new QCheckBox(text("ui.connect_points"));
    pf->addRow(m_line);
    m_pen = combo("curvePen");
    m_pen->addItems({text("ui.solid"), text("ui.dash"), text("ui.dot"), text("ui.dash_dot"),
                     text("ui.dash_dot_dot")});
    pf->addRow(text("ui.line_pattern"), m_pen);
    m_connection = combo("curveConnection");
    m_connection->addItems({text("ui.line"), text("ui.step_left"), text("ui.step_right"),
                            text("ui.step_center"), text("ui.impulse")});
    pf->addRow(text("ui.connection"), m_connection);
    section(text("ui.style_curve_markers"));
    m_marker = combo("curveMarker");
    m_marker->addItems({text("ui.none"), text("ui.circle"), text("ui.square"), text("ui.triangle"),
                        text("ui.cross"), text("ui.plus"), text("ui.diamond"), text("ui.star"),
                        text("ui.inverted_triangle"), text("ui.cross_square"),
                        text("ui.plus_square"), text("ui.cross_circle"), text("ui.plus_circle"),
                        text("ui.peace")});
    pf->addRow(text("ui.marker"), m_marker);
    m_markerSize = new DecimalEditor;
    m_markerSize->setRange(1, 20);
    m_markerSize->setDecimals(1);
    m_markerSize->setSingleStep(1);
    pf->addRow(text("ui.marker_size"), m_markerSize);
    m_markerFilled = new QCheckBox(text("ui.custom_marker_fill"));
    pf->addRow(m_markerFilled);
    m_markerColor = new ColorButton;
    pf->addRow(text("ui.marker_fill"), m_markerColor);
    m_markerBorder = new ColorButton;
    m_customBorder = new QCheckBox(text("ui.custom_marker_border"));
    pf->addRow(m_customBorder);
    pf->addRow(text("ui.marker_border_color"), m_markerBorder);
    section(text("ui.style_curve_fill"));
    m_fill = new QCheckBox(text("ui.fill_to_baseline"));
    m_fill->setObjectName("curveFill");
    pf->addRow(m_fill);
    m_fillColor = new ColorButton;
    m_fillColor->setObjectName("curveFillColor");
    pf->addRow(text("ui.fill_color_alpha"), m_fillColor);
    m_brush = combo("curveBrush");
    m_brush->addItems({text("ui.solid_fill"), "Dense 1", "Dense 2", "Dense 3", "Dense 4", "Dense 5",
                       "Dense 6", "Dense 7", text("ui.horizontal"), text("ui.vertical"),
                       text("ui.cross_hatch"), text("ui.backward_diagonal"),
                       text("ui.forward_diagonal"), text("ui.diagonal_cross")});
    pf->addRow(text("ui.fill_pattern"), m_brush);
    auto syncCurveControls = [this]
    {
        for (auto *w : QList<QWidget *>{m_width, m_pen, m_connection}) w->setEnabled(m_line->isChecked());
        const bool markers = m_marker->currentIndex() != 0;
        for (auto *w : QList<QWidget *>{m_markerSize, m_markerFilled, m_customBorder}) w->setEnabled(markers);
        m_markerColor->setEnabled(markers && m_markerFilled->isChecked());
        m_markerBorder->setEnabled(markers && m_customBorder->isChecked());
        m_fillColor->setEnabled(m_fill->isChecked());
        m_brush->setEnabled(m_fill->isChecked());
    };
    for (auto *b : {m_line, m_markerFilled, m_customBorder, m_fill})
        connect(b, &QCheckBox::toggled, this, syncCurveControls);
    connect(m_marker, qOverload<int>(&QComboBox::currentIndexChanged), this, syncCurveControls);
    syncCurveControls();
    cr->addWidget(m_curveFields);
    cr->addStretch();
    tabs->addTab(verticalScroller(curves), text("ui.curves"));
    m_heatmap = new HeatmapStyleEditor;
    tabs->addTab(verticalScroller(m_heatmap), text("ui.heatmap_tab"));
    layout->addWidget(tabs, 1);
    m_error = new QLabel;
    m_error->setWordWrap(true);
    m_error->setObjectName("Error");
    m_error->hide();
    layout->addWidget(m_error);
    auto *buttons = new QHBoxLayout;
    auto *reset = new QPushButton(text("ui.reset"));
    reset->setObjectName("resetPlotStyle");
    buttons->addStretch();
    buttons->addWidget(reset);
    layout->addLayout(buttons);
    connect(reset, &QPushButton::clicked, this,
            [this]
            {
                m_liveTimer.stop();
                if (m_view)
                    m_view->resetStyle();
            });
    connect(m_axis, &QTabBar::currentChanged, this,
            [this](int index)
            {
                if (m_loading)
                    return;
                storeAxis();
                m_axisIndex = index;
                loadAxis();
                scheduleApply();
            });
    connect(m_curve, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]
            {
                if (m_loading)
                    return;
                storeCurve();
                m_curveKey = m_curve->currentData().toString();
                loadCurve();
                scheduleApply();
            });
    connect(m_manual, &QCheckBox::toggled, this,
            [this](bool enabled)
            {
                m_lower->setEnabled(enabled);
                m_upper->setEnabled(enabled);
            });
    connect(capture, &QPushButton::clicked, this,
            [this]
            {
                storeAxis();
                if (!m_view)
                    return;
                auto view = m_view->styleWithViewport();
                auto &a = m_axisIndex ? m_draft.y : m_draft.x;
                auto current = m_axisIndex ? view.y : view.x;
                a.lower = current.lower;
                a.upper = current.upper;
                a.manualRange = true;
                loadAxis();
                scheduleApply();
            });
    m_liveTimer.setSingleShot(true);
    connect(&m_liveTimer, &QTimer::timeout, this, [this] { apply(); });
    connectLiveControls();
    setView(view);
}
void PlotStyleEditor::scheduleApply(int delayMs)
{
    if (!m_loading && !m_applying && m_view && m_view->hasPlot())
        m_liveTimer.start(delayMs);
}
void PlotStyleEditor::connectLiveControls()
{
    for (auto *control : findChildren<QCheckBox *>())
        connect(control, &QCheckBox::toggled, this, [this] { scheduleApply(); });
    for (auto *control : findChildren<QComboBox *>())
        if (control != m_curve)
            connect(control, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    [this] { scheduleApply(); });
    for (auto *control : findChildren<QDoubleSpinBox *>())
        connect(control, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this] { scheduleApply(60); });
    for (auto *control : findChildren<QSpinBox *>())
        connect(control, qOverload<int>(&QSpinBox::valueChanged), this,
                [this] { scheduleApply(60); });
    for (auto *control : findChildren<QLineEdit *>())
    {
        // Spin boxes have internal line edits; valueChanged already handles them.
        if (qobject_cast<QAbstractSpinBox *>(control->parentWidget())) continue;
        connect(control, &QLineEdit::textChanged, this, [this] { scheduleApply(160); });
        connect(control, &QLineEdit::editingFinished, this,
                [this] { if (m_liveTimer.isActive()) apply(); });
    }
    for (auto *control : findChildren<ColorButton *>())
        connect(control, &ColorButton::colorChanged, this, [this] { scheduleApply(); });

}
void PlotStyleEditor::setView(PlotView *view)
{
    if (m_view == view && view)
        return;
    if (m_liveTimer.isActive()) apply(); // Commit to the old view before rebinding.
    m_liveTimer.stop();
    if (m_view)
        disconnect(m_view, nullptr, this, nullptr);
    m_view = view;
    if (m_view)
    {
        connect(m_view, &PlotView::styleChanged, this, &PlotStyleEditor::reload);
        connect(m_view, &PlotView::outputChanged, this, &PlotStyleEditor::reload);
        connect(m_view, &PlotView::displayDataChanged, this, &PlotStyleEditor::refreshDisplayInfo);
    }
    reload();
}
void PlotStyleEditor::refreshDisplayInfo()
{
    if (!m_view) return;
    QScopedValueRollback<bool> loading(m_loading,true);
    m_heatmap->setStatus(m_view->heatmapStatus(),m_view->hasSignificanceGrid());
    const auto viewport=m_view->styleWithViewport();
    if (!m_view->style().colorManual)
        m_heatmap->setAutomaticRange(viewport.colorLower,viewport.colorUpper);
}
void PlotStyleEditor::reload()
{
    if (m_applying) return; // Keep cursor and partially entered text during live editing.
    m_liveTimer.stop();
    if (!m_view)
    {
        setEnabled(false);
        return;
    }
    m_loading = true;
    m_draft = m_view->style();
    const auto viewport = m_view->styleWithViewport();
    if (!m_draft.x.manualRange)
    {
        m_draft.x.lower = viewport.x.lower;
        m_draft.x.upper = viewport.x.upper;
    }
    if (!m_draft.y.manualRange)
    {
        m_draft.y.lower = viewport.y.lower;
        m_draft.y.upper = viewport.y.upper;
    }
    if (!m_draft.colorManual)
    { m_draft.colorLower=viewport.colorLower; m_draft.colorUpper=viewport.colorUpper; }
    setEnabled(m_view->hasPlot());
    m_canvasDetails->load(m_draft);
    const bool grid = dataAs<GridData>(m_view->currentData());
    m_tabs->setTabVisible(3, grid);
    m_heatmap->load(m_draft, m_view->heatmapStatus(), m_view->hasSignificanceGrid());
    loadAxis();
    auto old = m_curveKey;
    m_curve->clear();
    for (auto it = m_draft.curves.begin(); it != m_draft.curves.end(); ++it)
        m_curve->addItem(portLabel(it.key()), it.key());
    int index = m_curve->findData(old);
    if (index < 0)
        index = m_curve->findData(m_view->currentPort());
    m_curve->setCurrentIndex(index >= 0 ? index : 0);
    m_curveKey = m_curve->currentData().toString();
    loadCurve();
    m_error->hide();
    m_loading = false;
}
void PlotStyleEditor::storeAxis()
{
    auto &a = m_axisIndex ? m_draft.y : m_draft.x;
    m_axisDetails->store(a);
    a.label = m_axisLabel->text();
    a.logarithmic = m_log->isChecked();
    a.reversed = m_reverse->isChecked();
    a.grid = m_grid->isChecked();
    a.manualRange = m_manual->isChecked();
    // Keep exact stored bounds unless the displayed text was actually changed.
    if (m_lower->text() != displayedNumber(a.lower))
        a.lower = value(m_lower);
    if (m_upper->text() != displayedNumber(a.upper))
        a.upper = value(m_upper);
}
void PlotStyleEditor::loadAxis()
{
    QScopedValueRollback<bool> loading(m_loading, true);
    const auto &a = m_axisIndex ? m_draft.y : m_draft.x;
    m_axisDetails->load(a);
    m_axisLabel->setText(a.label);
    m_log->setChecked(a.logarithmic);
    m_log->setEnabled(m_axisIndex ? m_view->canLogY() : m_view->canLogX());
    m_reverse->setChecked(a.reversed);
    m_grid->setChecked(a.grid);
    m_manual->setChecked(a.manualRange);
    m_lower->setText(displayedNumber(a.lower));
    m_upper->setText(displayedNumber(a.upper));
    m_lower->setEnabled(a.manualRange);
    m_upper->setEnabled(a.manualRange);
}
void PlotStyleEditor::storeCurve()
{
    if (!m_draft.curves.contains(m_curveKey))
        return;
    auto &c = m_draft.curves[m_curveKey];
    c.inLegend = m_inLegend->isChecked();
    c.connection = m_connection->currentIndex() + 1;
    c.fill = m_fill->isChecked();
    c.fillColor = m_fillColor->color();
    c.markerFilled = m_markerFilled->isChecked();
    c.markerColor = m_markerColor->color();
    c.fillPattern = Qt::BrushStyle(m_brush->currentIndex() + 1);
    c.customMarkerBorder = m_customBorder->isChecked();
    c.markerBorderColor = m_markerBorder->color();
    c.name = m_curveName->text();
    c.visible = m_visible->isChecked();
    c.color = m_color->color();
    c.width = m_width->value();
    c.line = m_line->isChecked();
    c.pen = Qt::PenStyle(m_pen->currentIndex() + 1);
    c.marker = MarkerStyle(m_marker->currentIndex());
    c.markerSize = m_markerSize->value();
}
void PlotStyleEditor::loadCurve()
{
    QScopedValueRollback<bool> loading(m_loading, true);
    m_curveFields->setEnabled(m_draft.curves.contains(m_curveKey));
    if (!m_draft.curves.contains(m_curveKey))
        return;
    const auto &c = m_draft.curves[m_curveKey];
    m_inLegend->setChecked(c.inLegend);
    m_connection->setCurrentIndex(c.connection - 1);
    m_fill->setChecked(c.fill);
    m_fillColor->setColor(c.fillColor);
    m_markerFilled->setChecked(c.markerFilled);
    m_markerColor->setColor(c.markerColor);
    m_brush->setCurrentIndex(int(c.fillPattern) - 1);
    m_customBorder->setChecked(c.customMarkerBorder);
    m_markerBorder->setColor(c.markerBorderColor);
    m_curveName->setText(c.name);
    m_visible->setChecked(c.visible);
    m_color->setColor(c.color);
    m_width->setValue(c.width);
    m_line->setChecked(c.line);
    m_pen->setCurrentIndex(int(c.pen) - 1);
    m_marker->setCurrentIndex(int(c.marker));
    m_markerSize->setValue(c.markerSize);
}
bool PlotStyleEditor::apply()
{
    m_liveTimer.stop();
    if (!m_view || !m_view->hasPlot())
        return false;
    storeAxis();
    storeCurve();
    m_canvasDetails->store(m_draft);
    if (dataAs<GridData>(m_view->currentData())) m_heatmap->store(m_draft);
    // The view may normalize theme colors; do not reload all text fields and
    // reset cursor positions on each edit.
    const auto draft = m_draft;
    QScopedValueRollback<bool> applying(m_applying, true);
    const auto error = m_view->applyStyle(draft);
    if (error.isEmpty())
    {
        m_draft = m_view->style();
        QScopedValueRollback<bool> loading(m_loading, true);
        m_canvasDetails->setFollowingTheme(m_draft.followTheme);
        refreshDisplayInfo();
    }
    m_error->setText(error);
    m_error->setVisible(!error.isEmpty());
    return error.isEmpty();
}
} // namespace aster::ui
