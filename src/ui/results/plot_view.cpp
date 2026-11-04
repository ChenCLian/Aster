#include <QImageReader>
#include "ui/settings/theme_manager.h"
#include "io/exports/export_path.h"
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include "ui/results/plot_view.h"
#include "ui/results/grid_contours.h"
#include "ui/results/contour_plottable.h"
#include "ui/common/presentation.h"
#include "third_party/qcustomplot/qcustomplot.h"
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QTableView>
#include <QHeaderView>
#include <QAbstractTableModel>
#include <QMenu>
#include <QFile>
#include <QFileInfo>
#include "io/exports/csv_export.h"
#include "io/database/result_repository.h"
#include <QTextStream>
#include <QClipboard>
#include <QApplication>
#include <algorithm>
#include <cmath>
#include <limits>
#include <QEvent>
namespace aster::ui
{
class NumericalTableModel : public QAbstractTableModel
{
  public:
    explicit NumericalTableModel(QObject *p) : QAbstractTableModel(p) {}
    void setDataRef(DataRef d)
    {
        beginResetModel();
        m_data = std::move(d);
        endResetModel();
    }
    int rowCount(const QModelIndex &p = {}) const override
    {
        auto *d = dataAs<TableData>(m_data);
        return !p.isValid() && d ? int(d->rows.size()) : 0;
    }
    int columnCount(const QModelIndex &p = {}) const override
    {
        auto *d = dataAs<TableData>(m_data);
        return !p.isValid() && d ? int(d->columns.size()) : 0;
    }
    QVariant data(const QModelIndex &i, int role) const override
    {
        auto *d = dataAs<TableData>(m_data);
        if (!d || !i.isValid() || i.row() >= d->rows.size() || i.column() >= d->columns.size())
            return {};
        if (role == Qt::DisplayRole)
            return QString::number(d->rows[i.row()][i.column()], 'g', 10);
        if (role == Qt::TextAlignmentRole)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return {};
        auto *d = dataAs<TableData>(m_data);
        if (orientation == Qt::Horizontal && d && section < d->columns.size())
            return translatedLabel("column."+d->columns[section], d->columns[section]);
        return section + 1;
    }

  private:
    DataRef m_data;
};
namespace
{
class PlotTitle : public QCPTextElement
{
  public:
    using QCPTextElement::QCPTextElement;

  protected:
    QSize minimumOuterSizeHint() const override
    {
        return visible() ? QSize(0, QCPTextElement::minimumOuterSizeHint().height()) : QSize(0, 0);
    }
    QSize maximumOuterSizeHint() const override
    {
        return visible() ? QCPTextElement::maximumOuterSizeHint() : QSize(QWIDGETSIZE_MAX, 0);
    }
};
class OptionalColorScale : public QCPColorScale
{
  public:
    using QCPColorScale::QCPColorScale;
    void update(UpdatePhase phase) override
    {
        if (visible())
        {
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            QCPColorScale::update(phase);
        }
        else
        {
            setMinimumSize(0, 0);
            setMaximumSize(0, QWIDGETSIZE_MAX);
            QCPLayoutElement::update(phase);
        }
    }
    QSize minimumOuterSizeHint() const override
    {
        return visible() ? QCPColorScale::minimumOuterSizeHint() : QSize(0, 0);
    }
    QSize maximumOuterSizeHint() const override
    {
        return visible() ? QCPColorScale::maximumOuterSizeHint() : QSize(0, QWIDGETSIZE_MAX);
    }
};
QString axisLabel(const AxisInfo &a)
{
    return a.unit.isEmpty() ? a.name : a.name + " (" + a.unit + ")";
}
QPen penFor(const QString &port, double width, const QColor &main)
{
    if (port == "spectrum" || port == "series" || port == "detrended")
        return QPen(main, width);
    if (port == "trend" || port == "ar1_smoothed")
        return QPen(themeColor("purpleCurve"), 1.5, Qt::DashDotLine);
    if (port == "mc_family_threshold")
        return QPen(themeColor("purpleCurve"), 1.8, Qt::DashLine);
    bool ar1 = port.startsWith("ar1");
    QColor c = themeColor(ar1 ? "secondaryCurve" : "tertiaryCurve");
    if (port.contains("background"))
        return QPen(c, 1.9, ar1 ? Qt::SolidLine : Qt::DashLine);
    if (port.startsWith("fdr"))
        return QPen(themeColor("purpleCurve"), 1.1, Qt::DashDotDotLine);
    if (port.endsWith("999"))
        c = c.darker(130);
    else if (port.endsWith("90"))
        c = c.lighter(135);
    return QPen(c, 1.25, ar1 ? Qt::DashLine : Qt::DotLine);
}
bool increasing(const QVector<double> &values)
{
    return values.size() >= 2 &&
           std::adjacent_find(values.begin(), values.end(),
                              [](double a, double b) { return b <= a; }) == values.end();
}
bool geometricAxis(const QVector<double> &values)
{
    if (!increasing(values) || values.front() <= 0)
        return false;
    const double delta = std::log2(values[1]) - std::log2(values[0]);
    for (qsizetype i = 2; i < values.size(); ++i)
        if (std::abs(std::log2(values[i]) - std::log2(values[i - 1]) - delta) >
            std::abs(delta) * 1e-6)
            return false;
    return true;
}
qsizetype nearest(const QVector<double> &values, double coordinate)
{
    auto it = std::lower_bound(values.begin(), values.end(), coordinate);
    if (it == values.begin())
        return 0;
    if (it == values.end())
        return values.size() - 1;
    return coordinate - *(it - 1) <= *it - coordinate ? it - values.begin() - 1
                                                      : it - values.begin();
}
} // namespace
QCPAxis *PlotView::keyAxis() const
{
    return m_style.swapXY ? m_plot->yAxis : m_plot->xAxis;
}
QCPAxis *PlotView::valueAxis() const
{
    return m_style.swapXY ? m_plot->xAxis : m_plot->yAxis;
}
bool PlotView::eventFilter(QObject *w, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonPress)
        emit activated();
    if (w == m_plot && (e->type() == QEvent::Resize || e->type() == QEvent::Show))
        m_presentTimer.start();
    if (w == m_plot && e->type() == QEvent::Resize && m_reference.valid())
        m_viewportTimer.start();
    return QWidget::eventFilter(w, e);
}
PlotView::PlotView(QWidget *p) : QWidget(p)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget;
    layout->addWidget(m_stack);
    m_empty = new QLabel(text("ui.select_a_record_to_explore_its_plots_and_steps"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setObjectName("Muted");
    m_stack->addWidget(m_empty);
    m_plot = new QCustomPlot;
    m_plot->setObjectName("resultPlot");
    m_plot->installEventFilter(this);
    m_stack->addWidget(m_plot);
    m_presentTimer.setSingleShot(true);
    m_presentTimer.setInterval(0);
    connect(&m_presentTimer, &QTimer::timeout, this, [this] {
        if (hasPlot() && m_plot->isVisible())
            m_plot->replot(QCustomPlot::rpQueuedRefresh);
    });
    m_probe = new PlotProbe(m_plot, [this](QPointF point) { return sampleAt(point); });
    m_plot->setFocusPolicy(Qt::StrongFocus);
    m_plot->setToolTip(text("ui.probe_hint"));
    m_plot->setBackground(Qt::white);
    connect(m_plot->xAxis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged), m_plot->xAxis2,
            qOverload<const QCPRange &>(&QCPAxis::setRange));
    connect(m_plot->yAxis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged), m_plot->yAxis2,
            qOverload<const QCPRange &>(&QCPAxis::setRange));
    m_viewportTimer.setSingleShot(true);
    m_viewportTimer.setInterval(PlotLimits::debounceMs);
    connect(&m_viewportTimer, &QTimer::timeout, this,
            [this]
            {
                if (hasPlot() && m_reference.valid() && !m_rendering)
                    emit viewportChanged();
            });
    for (auto *axis : {m_plot->xAxis, m_plot->yAxis})
        connect(axis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged), this,
                [this]
                {
                    if (!m_rendering && m_reference.valid())
                        m_viewportTimer.start();
                });
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->setNoAntialiasingOnDrag(true);
    // Dense solid polylines otherwise take the expensive joined-path route in
    // Qt's raster painter. QCustomPlot keeps the full path for vector export.
    m_plot->setPlottingHint(QCP::phFastPolylines, true);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this,
            [this]
            {
                if (!hasPlot())
                    return;
                auto next = styleWithViewport();
                auto defaultsNow = defaults();
                if (next.followTheme)
                    for (auto i = next.curves.begin(); i != next.curves.end(); ++i)
                        if (m_defaults.curves.contains(i.key()) &&
                            i->color == m_defaults.curves[i.key()].color)
                            i->color = defaultsNow.curves[i.key()].color;
                for (auto i = next.curves.begin(); i != next.curves.end(); ++i)
                    i->themeDefaultColor = defaultsNow.curves[i.key()].themeDefaultColor;
                m_defaults = defaultsNow;
                applyStyle(next);
            });
    for (auto *axis : {keyAxis(), valueAxis()})
    {
        axis->setBasePen(QPen(themeColor("axis")));
        axis->setTickPen(QPen(themeColor("axis")));
        axis->setSubTicks(false);
        axis->grid()->setPen(QPen(themeColor("grid"), 1, Qt::DotLine));
        axis->setTickLabelColor(themeColor("muted"));
        axis->setLabelColor(themeColor("plotText"));
    }
    m_plot->legend->setBrush(QColor(255, 255, 255, 230));
    m_plot->legend->setBorderPen(QPen(themeColor("border")));
    QFont legendFont = font();
    legendFont.setPointSizeF(8.5);
    m_plot->legend->setFont(legendFont);
    m_plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);
    m_plot->plotLayout()->insertRow(0);
    m_title = new PlotTitle(m_plot, QString(), QFont(font().family(), 12, QFont::DemiBold));
    m_plot->plotLayout()->addElement(0, 0, m_title);
    m_title->setVisible(false);
    m_scale = new OptionalColorScale(m_plot);
    m_plot->plotLayout()->addElement(1, 1, m_scale);
    m_scale->setType(QCPAxis::atRight);
    m_scale->setVisible(false);
    m_table = new QTableView;
    m_table->setObjectName("numericResultTable");
    m_table->viewport()->installEventFilter(this);
    m_tableModel = new NumericalTableModel(this);
    m_table->setModel(m_tableModel);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setDefaultSectionSize(120);
    m_table->verticalHeader()->setDefaultSectionSize(28);
    m_stack->addWidget(m_table);
    m_plot->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_plot, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos)
            {
                auto *menu = new QMenu(this);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                auto *fitAction = menu->addAction(text("ui.fit_data"), this, &PlotView::fit);
                fitAction->setEnabled(plottedCount() > 0);
                auto *copy = menu->addAction(text("ui.copy_image"), this,
                                             [this]
                                             {
                                                 if (plottedCount() > 0)
                                                     QApplication::clipboard()->setPixmap(
                                                         m_plot->toPixmap());
                                             });
                copy->setEnabled(plottedCount() > 0);
                menu->addAction(text("ui.clear_plot"), this, [this] { clear(); });
                menu->popup(m_plot->mapToGlobal(pos));
            });
}
PlotStyle PlotView::defaults() const
{
    PlotStyle style;
    style.background = themeColor("plot");
    for (auto *a : {&style.x, &style.y})
    {
        a->axisColor = themeColor("axis");
        a->gridColor = themeColor("grid");
        a->subGridColor = themeColor("subGrid");
    }
    if (const auto *d = dataAs<SeriesData>(m_data))
    {
        style.x.label = axisLabel(d->xAxis);
        style.y.label = axisLabel(d->yAxis);
    }
    else if (const auto *d = dataAs<SpectrumData>(m_data))
    {
        style.x.label = axisLabel(d->frequencyAxis);
        style.y.label = axisLabel(d->powerAxis);
    }
    else if (const auto *d = dataAs<GridData>(m_data))
    {
        style.x.label = axisLabel(d->xAxis);
        style.y.label = axisLabel(d->yAxis);
        style.y.logarithmic = geometricAxis(d->y);
    }
    for (auto it = m_result.outputs.begin(); it != m_result.outputs.end(); ++it)
    {
        bool include =
            it.key() == m_port && !dataAs<TableData>(m_data) && !dataAs<GridData>(m_data);
        if (dataAs<SpectrumData>(m_data))
            include = compatibleCurveAxes(m_data, it.value()) || it.key() == "peaks";
        if (dataAs<SeriesData>(m_data))
            include = compatibleCurveAxes(m_data, it.value());
        if (dataAs<GridData>(m_data))
            include = it.key() == "coi" && dataAs<SeriesData>(it.value());
        if (!include)
            continue;
        CurveStyle c;
        c.name = m_result.report.value(it.key()).toString(portLabel(it.key()));
        auto pen = penFor(it.key(), 2., themeColor("curve"));
        c.color = pen.color();
        c.width = pen.widthF();
        c.pen = pen.style();
        c.visible = it.key().startsWith("overlay_") || it.key() == m_port || it.key() == "coi" ||
                    (m_port == "spectrum" &&
                     QStringList{"ar1_background", "ar1_95", "swa_background", "swa_95", "mc_background", "mc_family_threshold"}.contains(
                         it.key()));
        if (it.key() == m_port)
        {
            c.color = themeColor("curve");
            c.width = 2.;
            c.pen = Qt::SolidLine;
        }
        if (it.key().startsWith("overlay_"))
        {
            const QColor colors[] = {themeColor("secondaryCurve"), themeColor("tertiaryCurve"),
                                     themeColor("purpleCurve"), themeColor("pinkCurve")};
            c.color = colors[it.key().section('_', 1).toInt() % 4];
            c.width = 1.8;
        }
        if (it.key() == "coi")
        {
            c.name = text("ui.coi");
            c.color = themeColor("axis");
            c.width = 1.4;
            c.pen = Qt::DashLine;
        }
        if (it.key() == "peaks")
        {
            c.visible = m_port == "spectrum";
            c.color = themeColor("secondaryCurve");
            c.line = false;
            c.marker = MarkerStyle::Circle;
        }
        c.themeDefaultColor = c.color;
        style.curves.insert(it.key(), c);
    }
    return style;
}
bool PlotView::hasPlot() const
{
    return m_data && !dataAs<TableData>(m_data) && m_plot->plottableCount() > 0;
}
namespace
{
bool positiveCoordinates(const DataRef &data, bool x)
{
    const QVector<double> *values = nullptr;
    if (const auto *d = dataAs<SeriesData>(data))
        values = x ? &d->x : &d->y;
    if (const auto *d = dataAs<SpectrumData>(data))
        values = x ? &d->frequency : &d->power;
    if (const auto *d = dataAs<GridData>(data))
        values = x ? &d->x : &d->y;
    return values && std::any_of(values->begin(), values->end(), [](double v) { return v > 0; });
}
} // namespace
bool PlotView::canLogX() const
{
    return !dataAs<GridData>(m_data) && positiveCoordinates(m_data, true);
}
bool PlotView::canLogY() const
{
    if (const auto *grid = dataAs<GridData>(m_data))
        return increasing(grid->y) && grid->y.front() > 0;
    return positiveCoordinates(m_data, false);
}
double PlotView::displayedY(double y) const
{
    return m_gridLog ? std::log2(y) : y;
}
double PlotView::originalY(double y) const
{
    return m_gridLog ? std::pow(2., y) : y;
}
void PlotView::rememberView()
{
    if (m_viewKey.isEmpty() || !hasPlot())
        return;
    auto x = keyAxis()->range(), y = valueAxis()->range();
    m_views.insert(m_viewKey, new SavedView{m_style, x.lower, x.upper, originalY(y.lower),
                                            originalY(y.upper)});
}
void PlotView::showOutput(const NodeResult &result, const QString &port, const QString &viewKey)
{
    QScopedValueRollback<bool> rendering(m_rendering, true);
    m_viewportTimer.stop();
    rememberView();
    ++m_generation;
    m_viewKey.clear();
    m_reference = {};
    m_result = result;
    m_fullBounds = result.report["bounds"].toArray();
    m_port = port;
    m_data = result.outputs.value(port);
    const auto invalid = validateData(m_data);
    if (!invalid.isEmpty())
    {
        clear(invalid);
        return;
    }
    m_viewKey = viewKey;
    m_defaults = defaults();
    m_style = m_defaults;
    const auto *saved = viewKey.isEmpty() ? nullptr : m_views.object(viewKey);
    if (saved && saved->style.curves.keys() == m_defaults.curves.keys())
    {
        m_style = saved->style;
        // Cached output styles may predate an application theme change.
        if (m_style.followTheme)
        {
            m_style.background = m_defaults.background;
            for (auto *axis : {&m_style.x, &m_style.y})
            {
                axis->axisColor = themeColor("axis");
                axis->gridColor = themeColor("grid");
                axis->subGridColor = themeColor("subGrid");
            }
            for (auto it = m_style.curves.begin(); it != m_style.curves.end(); ++it)
            {
                if (it->color == it->themeDefaultColor)
                    it->color = m_defaults.curves[it.key()].color;
                it->themeDefaultColor = m_defaults.curves[it.key()].themeDefaultColor;
            }
        }
    }
    draw();
    if (saved && hasPlot())
    {
        keyAxis()->setRange(saved->xLow, saved->xHigh);
        valueAxis()->setRange(displayedY(saved->yLow), displayedY(saved->yHigh));
        m_plot->replot(QCustomPlot::rpQueuedReplot);
    }
    emit outputChanged();
    emit styleChanged();
}
void PlotView::clear(const QString &message)
{
    m_probe->dismiss();
    m_presentTimer.stop();
    QScopedValueRollback<bool> rendering(m_rendering, true);
    m_viewportTimer.stop();
    rememberView();
    ++m_generation;
    m_viewKey.clear();
    m_contour = nullptr;
    m_contourState = int(ContourState::NoBoundary);
    m_plot->clearPlottables();
    m_plot->clearItems();
    m_scale->setVisible(false);
    m_title->setVisible(false);
    m_plot->legend->setVisible(false);
    m_tableModel->setDataRef({});
    m_reference = {};
    m_data.reset();
    m_result = {};
    m_fullBounds = {};
    m_port.clear();
    m_style = {};
    m_defaults = {};
    m_empty->setText(message.isEmpty()
                         ? text("ui.plot_cleared_select_a_step_or_output_to_display_it_again")
                         : message);
    m_stack->setCurrentWidget(m_empty);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    emit outputChanged();
    emit styleChanged();
}
int PlotView::plottedCount() const
{
    int visible = 0;
    for (int i = 0; i < m_plot->plottableCount(); ++i)
        if (m_plot->plottable(i)->visible())
            ++visible;
    return visible;
}
bool PlotView::hasSignificanceGrid() const
{
    return dataAs<GridData>(m_data) && dataAs<GridData>(m_result.outputs.value("significance_ratio"));
}
int PlotView::significanceSegmentCount() const { return m_contour ? m_contour->segmentCount() : 0; }
QString PlotView::contourError() const
{
    if (!m_style.significanceContour) return {};
    if (m_contourState == int(ContourState::Invalid)) return text("ui.heatmap_contour_invalid");
    if (m_contourState == int(ContourState::TooDense)) return text("ui.heatmap_contour_dense");
    return {};
}
QString PlotView::heatmapStatus() const
{
    if (!dataAs<GridData>(m_data)) return {};
    QStringList lines;
    if (!contourError().isEmpty()) lines << contourError();
    else if (!hasSignificanceGrid()) lines << text("ui.heatmap_contour_missing");
    else if (!m_style.significanceContour) lines << text("ui.heatmap_contour_hidden");
    else if (!m_contour) lines << text("ui.heatmap_contour_none");
    else lines << text("ui.heatmap_contour_ready");
    if (m_result.report["display_reduced"].toBool()) lines << text("ui.heatmap_contour_reduced");
    if (m_style.colorLog && m_nonPositiveCells)
        lines << text("ui.heatmap_masked").arg(m_nonPositiveCells);
    return lines.join("\n");
}
QString PlotView::applyStyle(const PlotStyle &style)
{
    QScopedValueRollback<bool> rendering(m_rendering, true);
    if (!hasPlot())
        return text("ui.no_plot_to_edit");
    if (!style.background.isValid() || style.curves.keys() != m_defaults.curves.keys())
        return text("ui.style_does_not_match_the_current_output");
    if (int(style.legendPosition) < 0 || int(style.legendPosition) > 3)
        return text("error.invalid_legend_position");
    for (const auto &a : {style.x, style.y})
    {
        if (a.secondaryCount < 2 || a.secondaryCount > 30 || a.secondaryPrecision < 0 ||
            a.secondaryPrecision > 15 || !std::isfinite(a.secondaryWidth) ||
            a.secondaryWidth < .25 || a.secondaryWidth > 6 || !a.secondaryColor.isValid() ||
            a.secondaryPen < Qt::SolidLine || a.secondaryPen > Qt::DashDotDotLine ||
            !QStringList{"g", "f", "e", "eb"}.contains(a.secondaryFormat))
            return text("error.invalid_opposite_axis_style");
        if (a.tickCount < 2 || a.tickCount > 30 || a.precision < 0 || a.precision > 15 ||
            (a.logBase != 2 && a.logBase != 10) ||
            !QStringList{"g", "f", "e", "eb"}.contains(a.numberFormat))
            return text("error.invalid_axis_tick_settings");
        for (auto w : {a.axisWidth, a.gridWidth, a.subGridWidth})
            if (!std::isfinite(w) || w < .25 || w > 6)
                return text("error.invalid_axis_pen_width");
        for (auto c : {a.axisColor, a.gridColor, a.subGridColor})
            if (!c.isValid())
                return text("error.invalid_axis_color");
        for (auto p : {a.axisPen, a.gridPen, a.subGridPen})
            if (p < Qt::SolidLine || p > Qt::DashDotDotLine)
                return text("error.invalid_axis_pen");
        if (a.manualRange &&
            (!std::isfinite(a.lower) || !std::isfinite(a.upper) || a.lower >= a.upper ||
             !QCPRange::validRange(a.lower, a.upper) || (a.logarithmic && a.lower <= 0)))
            return text("ui.axis_limits_must_be_finite_and_ordered_logarithmic_limits_mu");
    }
    if ((style.x.logarithmic && !canLogX()) || (style.y.logarithmic && !canLogY()))
        return text("ui.this_output_does_not_support_the_selected_logarithmic_axis");
    for (const auto &c : style.curves)
    {
        if (!c.color.isValid() || !std::isfinite(c.width) || c.width < .25 || c.width > 10 ||
            !std::isfinite(c.markerSize) || c.markerSize < 1 || c.markerSize > 20 ||
            int(c.pen) < int(Qt::SolidLine) || int(c.pen) > int(Qt::DashDotDotLine) ||
            int(c.marker) < 0 || int(c.marker) > 13 || c.connection < 1 || c.connection > 5 ||
            !c.fillColor.isValid() || !c.markerColor.isValid() || !c.markerBorderColor.isValid() ||
            c.fillPattern < Qt::SolidPattern || c.fillPattern > Qt::DiagCrossPattern)
            return text("ui.invalid_curve_appearance");
    }
    for (auto size : {style.axisFontSize, style.legendFontSize, style.titleFontSize})
        if (!std::isfinite(size) || size < 6 || size > 32)
            return text("error.invalid_plot_font_size");
    if (style.colorGradient < 0 || style.colorGradient > 4 ||
        style.colorLevels < 2 || style.colorLevels > 1024 ||
        !style.significanceColor.isValid() || !std::isfinite(style.significanceWidth) ||
        style.significanceWidth < .25 || style.significanceWidth > 6 ||
        (style.colorManual &&
         (!std::isfinite(style.colorLower) || !std::isfinite(style.colorUpper) ||
          !QCPRange::validRange(style.colorLower, style.colorUpper) ||
          style.colorLower >= style.colorUpper || (style.colorLog && style.colorLower <= 0))))
        return text("ui.invalid_heat_map_color_range");
    if (style.colorLog && !m_style.colorLog && dataAs<GridData>(m_data) && m_positiveMax <= 0)
        return text("ui.heatmap_no_positive");
    QPixmap image;
    if (style.backgroundImage != m_backgroundPath && !style.backgroundImage.isEmpty())
    {
        QImageReader reader(style.backgroundImage);
        const auto size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > PlotLimits::maxExportPixels)
            return text("ui.invalid_background_image_or_pixel_limit_exceeded");
        image = QPixmap::fromImage(reader.read());
        if (image.isNull())
            return text("ui.cannot_read_background_image");
    }
    if (style.backgroundImage != m_backgroundPath)
    {
        m_backgroundImage = image;
        m_backgroundPath = style.backgroundImage;
    }
    const auto old = m_style;
    m_probe->dismiss();
    const bool scaleChanged = old.swapXY != style.swapXY ||
                              old.x.logarithmic != style.x.logarithmic ||
                              old.y.logarithmic != style.y.logarithmic;
    m_style = style;
    // Explicit canvas/axis colors detach the theme; toggling the application theme
    // passes unchanged style values and therefore keeps following it.
    if (style.background != old.background || style.x.axisColor != old.x.axisColor ||
        style.y.axisColor != old.y.axisColor || style.x.gridColor != old.x.gridColor ||
        style.y.gridColor != old.y.gridColor || style.x.subGridColor != old.x.subGridColor ||
        style.y.subGridColor != old.y.subGridColor)
        m_style.followTheme = false;
    if (m_style.followTheme)
    {
        m_style.background = themeColor("plot");
        for (auto *a : {&m_style.x, &m_style.y})
        {
            a->axisColor = themeColor("axis");
            a->gridColor = themeColor("grid");
            a->subGridColor = themeColor("subGrid");
        }
    }
    if (scaleChanged || old.colorLog != style.colorLog)
    {
        const auto xr = keyAxis()->range(), yr = valueAxis()->range();
        draw();
        if (!scaleChanged) { keyAxis()->setRange(xr); valueAxis()->setRange(yr); }
    }
    else
        applyAppearance();
    if (!scaleChanged && ((old.x.manualRange && !style.x.manualRange) ||
                          (old.y.manualRange && !style.y.manualRange)))
    {
        const auto xr = keyAxis()->range(), yr = valueAxis()->range();
        fitRanges();
        if (!(old.x.manualRange && !style.x.manualRange))
            keyAxis()->setRange(xr);
        if (!(old.y.manualRange && !style.y.manualRange))
            valueAxis()->setRange(yr);
    }
    auto updateRange =
        [](QCPAxis *axis, const AxisStyle &a, const AxisStyle &before, bool changed, auto transform)
    {
        if (a.manualRange &&
            (changed || !before.manualRange || a.lower != before.lower || a.upper != before.upper))
            axis->setRange(transform(a.lower), transform(a.upper));
    };
    updateRange(keyAxis(), style.x, old.x, scaleChanged, [](double v) { return v; });
    updateRange(valueAxis(), style.y, old.y, scaleChanged,
                [this](double v) { return displayedY(v); });
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    emit styleChanged();
    if (scaleChanged || style.x.manualRange != old.x.manualRange ||
        style.y.manualRange != old.y.manualRange || style.x.lower != old.x.lower ||
        style.x.upper != old.x.upper || style.y.lower != old.y.lower ||
        style.y.upper != old.y.upper)
        m_viewportTimer.start();
    return {};
}
PlotRequest PlotView::plotRequest(int width, int height) const
{
    PlotRequest request;
    const auto s = styleWithViewport();
    request.window = hasPlot();
    request.logX = s.x.logarithmic;
    request.logY = s.y.logarithmic;
    request.xLower = s.x.lower;
    request.xUpper = s.x.upper;
    request.yLower = s.y.lower;
    request.yUpper = s.y.upper;
    request.columns = width > 0 ? width : std::max(32, m_plot->axisRect()->width());
    request.rows = height > 0 ? height : std::max(32, m_plot->axisRect()->height());
    if (s.swapXY)
        std::swap(request.columns, request.rows);
    return request;
}
void PlotView::refreshOutput(const NodeResult &result)
{
    const auto previousStyle = m_style;
    const auto current = styleWithViewport();
    const auto bounds = m_fullBounds;
    const auto outputBounds = m_result.report.value("output_bounds");
    const auto reference = m_reference;
    const auto key = m_viewKey, port = m_port;
    {
        QSignalBlocker block(this);
        showOutput(result, port, key);
        m_fullBounds = bounds;
        if (outputBounds.isObject()) m_result.report["output_bounds"] = outputBounds;
        applyStyle(current);
        m_style.x.manualRange = previousStyle.x.manualRange;
        m_style.y.manualRange = previousStyle.y.manualRange;
        m_reference = reference;
        m_viewportTimer.stop();
    }
    // Update mapping diagnostics without resetting unfinished text fields.
    emit displayDataChanged();
}
PlotStyle PlotView::styleWithViewport() const
{
    auto style = m_style;
    if (hasPlot())
    {
        const auto x = keyAxis()->range(), y = valueAxis()->range();
        style.x.manualRange = style.y.manualRange = true;
        style.x.lower = x.lower;
        style.x.upper = x.upper;
        style.y.lower = originalY(y.lower);
        style.y.upper = originalY(y.upper);
        if (!style.colorManual)
            for (int i=0;i<m_plot->plottableCount();++i)
                if (auto *map=qobject_cast<QCPColorMap *>(m_plot->plottable(i)))
                { style.colorLower=map->dataRange().lower; style.colorUpper=map->dataRange().upper; break; }
    }
    return style;
}
void PlotView::resetStyle()
{
    if (hasPlot())
    {
        m_style = m_defaults;
        draw();
        emit styleChanged();
    }
}
void PlotView::setLegend(bool v)
{
    auto s = m_style;
    s.legend = v;
    applyStyle(s);
}
void PlotView::setLogY(bool v)
{
    auto s = m_style;
    s.y.logarithmic = v;
    s.y.manualRange = false;
    applyStyle(s);
}
void PlotView::setLineWidth(double w)
{
    auto s = m_style;
    if (s.curves.contains(m_port))
    {
        s.curves[m_port].width = w;
        applyStyle(s);
    }
}
void PlotView::setMainColor(QColor color)
{
    auto s = m_style;
    if (color.isValid() && s.curves.contains(m_port))
    {
        s.curves[m_port].color = color;
        applyStyle(s);
    }
}
void PlotView::fitRanges()
{
    if (!plottedCount())
        return;
    auto bounds = m_fullBounds;
    const auto byOutput = m_result.report["output_bounds"].toObject();
    if (!dataAs<GridData>(m_data) && !byOutput.isEmpty())
    {
        // Database previews may contain hidden or unrelated outputs. Their
        // ranges must not affect the selected graph. Retain full-resolution
        // bounds so fitting after a zoom does not fit only the loaded window.
        bounds = {};
        for (auto it = m_style.curves.cbegin(); it != m_style.curves.cend(); ++it)
        {
            if (!it->visible) continue;
            const auto b = byOutput[it.key()].toArray();
            if (b.size() != 4) continue;
            if (bounds.isEmpty()) bounds = b;
            else {
                bounds[0] = std::min(bounds[0].toDouble(), b[0].toDouble());
                bounds[1] = std::max(bounds[1].toDouble(), b[1].toDouble());
                bounds[2] = std::min(bounds[2].toDouble(), b[2].toDouble());
                bounds[3] = std::max(bounds[3].toDouble(), b[3].toDouble());
            }
        }
    }
    if (bounds.size() == 4)
    {
        double xl = bounds[0].toDouble(), xu = bounds[1].toDouble();
        double yl = bounds[2].toDouble(), yu = bounds[3].toDouble();
        // A constant axis has a valid full-data extent, but needs a nonzero
        // display span. Do not discard the other axis's full bounds and fit
        // only the currently loaded (zoomed) preview in this case.
        const auto expandConstant = [](double &lower, double &upper, bool logarithmic) {
            if (!std::isfinite(lower) || lower != upper) return;
            if (logarithmic) {
                if (lower > 0) { lower /= 1.1; upper *= 1.1; }
            } else {
                const auto padding = lower == 0 ? .5
                    : std::max(std::abs(lower) * .05, QCPRange::minRange * 10);
                lower -= padding;
                upper += padding;
            }
        };
        expandConstant(xl, xu, m_style.x.logarithmic);
        expandConstant(yl, yu, m_style.y.logarithmic);
        if ((!m_style.x.logarithmic || xl > 0) && (!m_style.y.logarithmic || yl > 0) &&
            xl < xu && yl < yu &&
            QCPRange::validRange(xl, xu) && QCPRange::validRange(displayedY(yl), displayedY(yu)))
        {
            keyAxis()->setRange(xl, xu);
            valueAxis()->setRange(displayedY(yl), displayedY(yu));
            if (!dataAs<GridData>(m_data))
            {
                keyAxis()->scaleRange(1.03);
                valueAxis()->scaleRange(1.08);
            }
            return;
        }
    }
    if (const auto *grid = dataAs<GridData>(m_data))
    {
        keyAxis()->setRange(grid->x.front(), grid->x.back());
        valueAxis()->setRange(displayedY(grid->y.front()), displayedY(grid->y.back()));
    }
    else
    {
        m_plot->rescaleAxes(true);
        keyAxis()->scaleRange(1.03);
        valueAxis()->scaleRange(1.08);
    }
}
void PlotView::fit()
{
    QScopedValueRollback<bool> rendering(m_rendering, true);
    if (!hasPlot())
        return;
    m_style.x.manualRange = m_style.y.manualRange = false;
    fitRanges();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    m_viewportTimer.start();
    emit styleChanged();
}
void PlotView::applyAppearance()
{
    if (m_backgroundPath != m_style.backgroundImage)
    {
        m_backgroundPath = m_style.backgroundImage;
        m_backgroundImage = {};
        if (!m_backgroundPath.isEmpty())
        {
            QImageReader reader(m_backgroundPath);
            const auto size = reader.size();
            if (size.isValid() &&
                qint64(size.width()) * size.height() <= PlotLimits::maxExportPixels)
                m_backgroundImage = QPixmap::fromImage(reader.read());
        }
    }
    m_plot->setBackground(m_style.background);
    m_plot->axisRect()->setBackground(m_style.background);
    m_plot->axisRect()->setBackground(m_backgroundImage, true,
                                      m_style.backgroundKeepAspect ? Qt::KeepAspectRatio
                                                                   : Qt::IgnoreAspectRatio);
    const bool dark = m_style.background.lightnessF() < .45;
    const auto textColor = m_style.followTheme ? themeColor("plotText")
                                              : themeColor(dark ? "onDarkPlot" : "onLightPlot");
    auto titleFont = m_title->font();
    titleFont.setPointSizeF(m_style.titleFontSize);
    m_title->setFont(titleFont);
    auto legendFont = m_plot->legend->font();
    legendFont.setPointSizeF(m_style.legendFontSize);
    m_plot->legend->setFont(legendFont);
    for (auto *axis :
         {m_plot->xAxis, m_plot->yAxis, m_plot->xAxis2, m_plot->yAxis2, m_scale->axis()})
    {
        auto f = axis->tickLabelFont();
        f.setPointSizeF(m_style.axisFontSize);
        axis->setTickLabelFont(f);
        axis->setLabelFont(f);
    }
    m_title->setTextColor(textColor);
    auto legendBackground = m_style.background;
    legendBackground.setAlpha(235);
    m_plot->legend->setBrush(legendBackground);
    m_plot->legend->setTextColor(textColor);
    m_plot->legend->setBorderPen(QPen(m_style.x.axisColor));
    for (auto *axis : {keyAxis(), valueAxis(), m_scale->axis()})
    {
        axis->setLabelColor(textColor);
        axis->setTickLabelColor(textColor);
    }
    m_title->setText(m_style.title);
    m_title->setVisible(!m_style.title.isEmpty());
    keyAxis()->setLabel(m_style.x.label);
    valueAxis()->setLabel(m_style.y.label);
    keyAxis()->setRangeReversed(m_style.x.reversed);
    valueAxis()->setRangeReversed(m_style.y.reversed);
    keyAxis()->grid()->setVisible(m_style.x.grid);
    valueAxis()->grid()->setVisible(m_style.y.grid);
    auto styleAxis = [&](QCPAxis *axis, QCPAxis *secondary, const AxisStyle &a)
    {
        axis->setVisible(a.axisVisible);
        axis->setBasePen(QPen(a.axisColor, a.axisWidth, a.axisPen));
        axis->setTickPen(QPen(a.axisColor, a.axisWidth));
        axis->setSubTickPen(QPen(a.axisColor, a.axisWidth));
        axis->setTicks(a.ticks);
        axis->setTickLabels(a.tickLabels);
        axis->setSubTicks(a.subTicks);
        axis->setNumberFormat(a.numberFormat);
        axis->setNumberPrecision(a.precision);
        axis->ticker()->setTickCount(a.tickCount);
        if (auto ticker = qSharedPointerDynamicCast<QCPAxisTickerLog>(axis->ticker()))
            ticker->setLogBase(a.logBase);
        axis->grid()->setPen(QPen(a.gridColor, a.gridWidth, a.gridPen));
        axis->grid()->setSubGridVisible(a.subGrid);
        axis->grid()->setSubGridPen(QPen(a.subGridColor, a.subGridWidth, a.subGridPen));
        secondary->setVisible(a.secondaryVisible);
        secondary->setScaleType(axis->scaleType());
        secondary->setTicker(axis->ticker());
        secondary->setRange(axis->range());
        secondary->setRangeReversed(a.reversed);
        secondary->setBasePen(axis->basePen());
        secondary->setTickPen(axis->tickPen());
        secondary->setSubTickPen(axis->subTickPen());
        secondary->setTicks(a.secondaryTicks);
        secondary->setTickLabels(a.secondaryLabels);
        secondary->setSubTicks(a.subTicks);
        secondary->setNumberFormat(a.numberFormat);
        secondary->setNumberPrecision(a.precision);
        secondary->setTickLabelColor(a.independentSecondary ? a.secondaryColor : textColor);
        if (a.independentSecondary)
        {
            QSharedPointer<QCPAxisTicker> ticker;
            if (axis->scaleType() == QCPAxis::stLogarithmic)
            {
                auto *t = new QCPAxisTickerLog;
                t->setLogBase(a.logBase);
                ticker.reset(t);
            }
            else if (axis == valueAxis() && m_gridLog)
                ticker = axis->ticker();
            else
                ticker.reset(new QCPAxisTicker);
            if (ticker != axis->ticker())
                ticker->setTickCount(a.secondaryCount);
            secondary->setTicker(ticker);
            secondary->setBasePen(QPen(a.secondaryColor, a.secondaryWidth, a.secondaryPen));
            secondary->setTickPen(QPen(a.secondaryColor, a.secondaryWidth));
            secondary->setSubTickPen(QPen(a.secondaryColor, a.secondaryWidth));
            secondary->setNumberFormat(a.secondaryFormat);
            secondary->setNumberPrecision(a.secondaryPrecision);
        }
    };
    styleAxis(keyAxis(), m_style.swapXY ? m_plot->yAxis2 : m_plot->xAxis2, m_style.x);
    styleAxis(valueAxis(), m_style.swapXY ? m_plot->xAxis2 : m_plot->yAxis2, m_style.y);
    for (int i = 0; i < m_plot->plottableCount(); ++i)
        if (auto *map = qobject_cast<QCPColorMap *>(m_plot->plottable(i)))
        {
            QCPColorGradient gradient;
            if (m_style.colorGradient == 0)
            {
                gradient.setColorStopAt(0, themeColor("heatmap0"));
                gradient.setColorStopAt(.25, themeColor("heatmap1"));
                gradient.setColorStopAt(.5, themeColor("heatmap2"));
                gradient.setColorStopAt(.75, themeColor("heatmap3"));
                gradient.setColorStopAt(1, themeColor("heatmap4"));
            }
            else
            {
                const QCPColorGradient::GradientPreset presets[] = {
                    QCPColorGradient::gpGrayscale, QCPColorGradient::gpThermal,
                    QCPColorGradient::gpPolar, QCPColorGradient::gpSpectrum};
                gradient.loadPreset(presets[m_style.colorGradient - 1]);
            }
            gradient.setNanHandling(QCPColorGradient::nhTransparent);
            gradient.setLevelCount(m_style.colorLevels);
            if (m_style.colorReverse) gradient = gradient.inverted();
            map->setGradient(gradient);
            m_scale->setVisible(m_style.colorScaleVisible);
            // setInterpolate invalidates the raster even when the value is unchanged.
            if (map->interpolate() != m_style.colorInterpolate)
                map->setInterpolate(m_style.colorInterpolate);
            map->setDataScaleType(m_style.colorLog ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
            if (m_style.colorManual)
                map->setDataRange(QCPRange(m_style.colorLower, m_style.colorUpper));
            else
            {
                double lo = m_style.colorLog ? m_positiveMin : m_colorMin;
                double hi = m_style.colorLog ? m_positiveMax : m_colorMax;
                if (!(hi > lo))
                {
                    if (m_style.colorLog) { lo /= 2; hi *= 2; }
                    else { const auto pad = std::max(1., std::abs(lo)) * .01; lo -= pad; hi += pad; }
                }
                map->setDataRange(QCPRange(lo, hi));
            }
        }
    if (m_contour)
    {
        m_contour->setVisible(m_style.significanceContour);
        m_contour->setPen(QPen(m_style.significanceColor, m_style.significanceWidth));
    }
    const Qt::Alignment positions[] = {Qt::AlignTop | Qt::AlignRight, Qt::AlignTop | Qt::AlignLeft,
                                       Qt::AlignBottom | Qt::AlignRight,
                                       Qt::AlignBottom | Qt::AlignLeft};
    m_plot->axisRect()->insetLayout()->setInsetAlignment(0, positions[int(m_style.legendPosition)]);
    const QCPScatterStyle::ScatterShape markers[] = {QCPScatterStyle::ssNone,
                                                     QCPScatterStyle::ssCircle,
                                                     QCPScatterStyle::ssSquare,
                                                     QCPScatterStyle::ssTriangle,
                                                     QCPScatterStyle::ssCross,
                                                     QCPScatterStyle::ssPlus,
                                                     QCPScatterStyle::ssDiamond,
                                                     QCPScatterStyle::ssStar,
                                                     QCPScatterStyle::ssTriangleInverted,
                                                     QCPScatterStyle::ssCrossSquare,
                                                     QCPScatterStyle::ssPlusSquare,
                                                     QCPScatterStyle::ssCrossCircle,
                                                     QCPScatterStyle::ssPlusCircle,
                                                     QCPScatterStyle::ssPeace};
    int visible = 0;
    for (int i = 0; i < m_plot->graphCount(); ++i)
    {
        auto *g = m_plot->graph(i);
        const auto key = g->property("outputKey").toString();
        const auto c = m_style.curves.value(key);
        g->setName(c.name);
        g->setVisible(c.visible);
        g->setPen(QPen(c.color, c.width, c.pen));
        g->setLineStyle(c.line ? QCPGraph::LineStyle(c.connection) : QCPGraph::lsNone);
        g->setBrush(c.fill ? QBrush(c.fillColor, c.fillPattern) : QBrush(Qt::NoBrush));
        g->setScatterStyle(QCPScatterStyle(
            markers[int(c.marker)],
            QPen(c.customMarkerBorder ? c.markerBorderColor : c.color, c.width),
            QBrush(c.markerFilled ? c.markerColor : m_style.background), c.markerSize));
        if (c.visible && c.inLegend)
        {
            g->addToLegend();
            ++visible;
        }
        else
            g->removeFromLegend();
    }
    m_plot->legend->setVisible(m_style.legend && visible > 0);
}
void PlotView::draw()
{
    m_probe->dismiss();
    QScopedValueRollback<bool> rendering(m_rendering, true);
    m_contour = nullptr;
    m_contourState = int(ContourState::NoBoundary);
    m_plot->clearPlottables();
    m_plot->clearItems();
    m_scale->setVisible(false);
    m_plot->legend->setVisible(false);
    m_gridLog = false;
    m_tableModel->setDataRef({});
    if (!m_data)
    {
        m_stack->setCurrentWidget(m_empty);
        return;
    }
    if (dataAs<TableData>(m_data))
    {
        m_tableModel->setDataRef(m_data);
        m_stack->setCurrentWidget(m_table);
        return;
    }
    m_stack->setCurrentWidget(m_plot);
    keyAxis()->setScaleType(QCPAxis::stLinear);
    valueAxis()->setScaleType(QCPAxis::stLinear);
    keyAxis()->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
    valueAxis()->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
    if (const auto *grid = dataAs<GridData>(m_data))
    {
        if (!increasing(grid->x) || !increasing(grid->y))
        {
            m_empty->setText(
                text("ui.heat_maps_need_at_least_two_increasing_coordinates_per_axis_"));
            m_stack->setCurrentWidget(m_empty);
            return;
        }
        auto *map = new QCPColorMap(keyAxis(), valueAxis());
        map->setColorScale(m_scale);
        int cols = int(std::min<qsizetype>(grid->columns, PlotLimits::maxDimension)),
            rows = int(std::min<qsizetype>(grid->rows, PlotLimits::maxDimension));
        while (qint64(cols) * rows > PlotLimits::maxHeatmapCells)
        {
            cols = (cols + 1) / 2;
            rows = (rows + 1) / 2;
        }
        const bool periodLog = m_style.y.logarithmic;
        m_gridLog = periodLog;
        auto yCoord = [&](double v) { return periodLog ? std::log2(v) : v; };
        map->data()->setSize(cols, rows);
        map->data()->setRange(QCPRange(grid->x.front(), grid->x.back()),
                              QCPRange(yCoord(grid->y.front()), yCoord(grid->y.back())));
        QVector<qsizetype> sourceX(cols), sourceY(rows);
        for (int c = 0; c < cols; ++c)
            sourceX[c] = nearest(grid->x, grid->x.front() + double(c) / (cols - 1) *
                                                                (grid->x.back() - grid->x.front()));
        for (int r = 0; r < rows; ++r)
        {
            const double yy =
                yCoord(grid->y.front()) +
                double(r) / (rows - 1) * (yCoord(grid->y.back()) - yCoord(grid->y.front()));
            sourceY[r] = nearest(grid->y, periodLog ? std::pow(2., yy) : yy);
        }
        m_colorMin = m_positiveMin = std::numeric_limits<double>::infinity();
        m_colorMax = -std::numeric_limits<double>::infinity();
        m_positiveMax = 0;
        m_nonPositiveCells = 0;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                const auto value = grid->values[sourceY[r] * grid->columns + sourceX[c]];
                map->data()->setCell(c, r, m_style.colorLog && value <= 0 ? qQNaN() : value);
                m_colorMin = std::min(m_colorMin, value); m_colorMax = std::max(m_colorMax, value);
                if (value > 0) { m_positiveMin = std::min(m_positiveMin,value); m_positiveMax = std::max(m_positiveMax,value); }
                else ++m_nonPositiveCells;
            }
        // A saved log style may be revisited on a viewport with no positive samples.
        // Keep the setting, render transparent cells, and report the empty mapping.
        if (m_positiveMax <= 0) { m_positiveMin = 1; }
        if (const auto *ratio = dataAs<GridData>(m_result.outputs.value("significance_ratio")))
        {
            GridContours outline;
            if (ratio->x != grid->x || ratio->y != grid->y)
                outline.state = ContourState::Invalid;
            else
            {
                auto displayGrid = *ratio; // Shared values; only the short Y axis detaches.
                if (periodLog) for (auto &y : displayGrid.y) y = std::log2(y);
                outline = gridContours(displayGrid);
            }
            m_contourState = int(outline.state);
            if (!outline.segments.isEmpty())
            {
                m_contour = new ContourPlottable(keyAxis(), valueAxis(), std::move(outline.segments));
            }
        }
        else if (m_result.outputs.contains("significance_ratio"))
            m_contourState = int(ContourState::Invalid);
        m_scale->setVisible(true);
        m_scale->axis()->setLabel(axisLabel(grid->valueAxis));
        keyAxis()->setLabel(axisLabel(grid->xAxis));
        valueAxis()->setLabel(axisLabel(grid->yAxis));
        if (periodLog)
        {
            auto ticker = QSharedPointer<QCPAxisTickerText>(new QCPAxisTickerText);
            double a = std::ceil(std::log2(grid->y.front())),
                   b = std::floor(std::log2(grid->y.back()));
            int step = std::max(1, int(std::ceil((b - a) / 7.)));
            for (double y = a; y <= b; y += step)
                ticker->addTick(y, QString::number(std::pow(2., y), 'g', 5));
            valueAxis()->setTicker(ticker);
        }
        if (const auto *coi = dataAs<SeriesData>(m_result.outputs.value("coi"));
            coi && validateData(m_result.outputs.value("coi")).isEmpty())
        {
            auto *line = m_plot->addGraph(keyAxis(), valueAxis());
            line->setName(text("ui.coi"));
            line->setProperty("outputKey", "coi");
            QVector<double> y = coi->y;
            for (auto &v : y)
                v = periodLog ? (v > 0 ? std::log2(v) : qQNaN()) : v;
            line->setData(coi->x, y);
            line->setPen(QPen(themeColor("axis"), 1.4, Qt::DashLine));
        }
        keyAxis()->setRange(grid->x.front(), grid->x.back());
        valueAxis()->setRange(yCoord(grid->y.front()), yCoord(grid->y.back()));
    }
    else
    {
        if (m_style.x.logarithmic)
        {
            keyAxis()->setScaleType(QCPAxis::stLogarithmic);
            keyAxis()->setTicker(QSharedPointer<QCPAxisTickerLog>(new QCPAxisTickerLog));
        }
        if (m_style.y.logarithmic)
        {
            valueAxis()->setScaleType(QCPAxis::stLogarithmic);
            valueAxis()->setTicker(QSharedPointer<QCPAxisTickerLog>(new QCPAxisTickerLog));
        }
        for (auto it = m_result.outputs.begin(); it != m_result.outputs.end(); ++it)
        {
            if (!m_style.curves.contains(it.key()))
                continue;
            QVector<double> x, y;
            AxisInfo xa, ya;
            if (const auto *data = dataAs<SeriesData>(it.value()))
            {
                x = data->x;
                y = data->y;
                xa = data->xAxis;
                ya = data->yAxis;
            }
            else if (const auto *data = dataAs<SpectrumData>(it.value()))
            {
                x = data->frequency;
                y = data->power;
                xa = data->frequencyAxis;
                ya = data->powerAxis;
            }
            else if (it.key() == "peaks" && m_port == "spectrum")
            {
                const auto *data = dataAs<TableData>(it.value());
                if (!data)
                    continue;
                int fx = data->columns.indexOf("frequency"), fy = data->columns.indexOf("power");
                if (fx < 0 || fy < 0)
                    continue;
                for (const auto &row : data->rows)
                {
                    x.push_back(row[fx]);
                    y.push_back(row[fy]);
                }
            }
            else
                continue;
            for (qsizetype i = 0; i < y.size(); ++i)
                if ((m_style.y.logarithmic && y[i] <= 0) || (m_style.x.logarithmic && x[i] <= 0))
                    y[i] = qQNaN();
            auto *graph = m_plot->addGraph(keyAxis(), valueAxis());
            graph->setName(portLabel(it.key()));
            graph->setProperty("outputKey", it.key());
            graph->setData(x, y);
            graph->setAdaptiveSampling(true);
            if (it.key() == m_port)
            {
                keyAxis()->setLabel(axisLabel(xa));
                valueAxis()->setLabel(axisLabel(ya));
            }
        }
    }
    applyAppearance();
    fitRanges();
    if (m_style.x.manualRange)
        keyAxis()->setRange(m_style.x.lower, m_style.x.upper);
    if (m_style.y.manualRange)
        valueAxis()->setRange(displayedY(m_style.y.lower), displayedY(m_style.y.upper));
    // Present newly installed data once after layout/show has settled. Interactive
    // updates still coalesce; no database reload is needed for a simple click.
    m_presentTimer.start();
}
std::optional<ProbeSample> PlotView::sampleAt(QPointF point) const
{
    if (!hasPlot()) return {};
    auto number = [](double v) { return QString::number(v, 'g', 12); };
    auto line = [&](const QString &name, double v) { return name + ": " + number(v); };
    auto note = [&] { return m_result.report["display_reduced"].toBool()
        ? "\n" + text("ui.probe_reduced") : QString(); };
    if (const auto *grid = dataAs<GridData>(m_data))
    {
        for (int i = 0; i < m_plot->plottableCount(); ++i)
        {
            auto *map = qobject_cast<QCPColorMap *>(m_plot->plottable(i));
            if (!map || !map->visible()) continue;
            double k = 0, v = 0;
            map->pixelsToCoords(point, k, v);
            if (!map->data()->keyRange().contains(k) || !map->data()->valueRange().contains(v)) return {};
            int c = 0, r = 0;
            map->data()->coordToCell(k, v, &c, &r);
            if (c < 0 || r < 0 || c >= map->data()->keySize() || r >= map->data()->valueSize()) return {};
            const double z = map->data()->cell(c, r);
            if (!std::isfinite(z)) return {}; // Includes masked cells on a log color scale.
            map->data()->cellToCoord(c, r, &k, &v);
            const auto ix = nearest(grid->x, k), iy = nearest(grid->y, originalY(v));
            return ProbeSample{map->coordsToPixels(k, v), portLabel(m_port),
                line(axisLabel(grid->xAxis), grid->x[ix]) + "\n" +
                line(axisLabel(grid->yAxis), grid->y[iy]) + "\n" +
                line(axisLabel(grid->valueAxis), z) + note()};
        }
        return {};
    }
    std::optional<ProbeSample> best;
    double distance = 14.0 * 14.0;
    for (int i = 0; i < m_plot->graphCount(); ++i)
    {
        auto *graph = m_plot->graph(i);
        if (!graph->visible() || graph->data()->isEmpty()) continue;
        const auto *axis = graph->keyAxis();
        const double p = axis->orientation() == Qt::Horizontal ? point.x() : point.y();
        const auto a = axis->pixelToCoord(p - 14), b = axis->pixelToCoord(p + 14);
        auto first = graph->data()->findBegin(std::min(a, b), false);
        const auto end = graph->data()->findEnd(std::max(a, b), false);
        for (auto it = first; it != end; ++it)
        {
            if (!std::isfinite(it->key) || !std::isfinite(it->value)) continue;
            const auto pixel = graph->coordsToPixels(it->key, it->value);
            if (!m_plot->axisRect()->rect().contains(pixel.toPoint())) continue;
            const auto delta = pixel - point;
            double d = delta.x() * delta.x() + delta.y() * delta.y();
            const bool peak = graph->property("outputKey").toString() == "peaks";
            if (d > distance || (best && d == distance && !peak)) continue;
            distance = d;
            QString detail = line(keyAxis()->label().isEmpty() ? "X" : keyAxis()->label(), it->key) + "\n" +
                line(valueAxis()->label().isEmpty() ? "Y" : valueAxis()->label(), it->value);
            if (const auto *spectrum = dataAs<SpectrumData>(m_data);
                spectrum && it->key > 0 && std::isfinite(1.0 / it->key))
            {
                const auto frequencyUnit = spectrum->frequencyAxis.unit.trimmed();
                const auto periodUnit = frequencyUnit.startsWith("1/") ? frequencyUnit.mid(2) :
                    (frequencyUnit.isEmpty() ? QString() : "1/(" + frequencyUnit + ")");
                detail += "\n" + line(axisLabel({text("ui.probe_period"), periodUnit}), 1.0 / it->key);
            }
            best = ProbeSample{pixel, graph->name(), detail + note()};
        }
    }
    return best;
}
QMenu *PlotView::curveMenu(QWidget *owner)
{
    auto *menu = new QMenu(owner);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto generation = m_generation;
    for (auto it = m_style.curves.begin(); it != m_style.curves.end(); ++it)
    {
        const auto key = it.key();
        auto *action = menu->addAction(it->name);
        action->setCheckable(true);
        action->setChecked(it->visible);
        connect(action, &QAction::toggled, this,
                [this, key, generation](bool visible)
                {
                    if (generation != m_generation || !hasPlot() || !m_style.curves.contains(key))
                        return;
                    auto s = m_style;
                    s.curves[key].visible = visible;
                    applyStyle(s);
                });
    }
    if (menu->isEmpty())
        menu->addAction(text("ui.no_curves_for_this_output"))->setEnabled(false);
    return menu;
}
QString PlotView::exportCsv(const QString &path) const
{
    if (m_reference.valid())
    {
        if (conflictsWithDatabase(path, {m_reference.databasePath}))
            return text("error.cannot_overwrite_database");
        ResultRepository repo;
        auto error = repo.open(m_reference.databasePath, false);
        if (!error.isEmpty())
            return error;
        auto output = repo.loadOutput(m_reference);
        return output.ok() ? exportDataCsv(output.data, path).error : output.error;
    }
    return exportDataCsv(m_data, path).error;
}
bool PlotView::exportPng(const QString &path) const
{
    return !conflictsWithDatabase(path, {m_reference.databasePath}) &&
           hasPlot() && m_plot->savePng(path, 1600, 900);
}
} // namespace aster::ui
