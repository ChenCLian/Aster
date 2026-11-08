// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/services/plot_data_loader.h"
#include "ui/results/plot_recipe.h"
#include "ui/settings/theme_manager.h"
#include "ui/results/plot_workspace.h"
#include "io/database/result_repository.h"
#include "ui/common/presentation.h"
#include "third_party/qcustomplot/qcustomplot.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QThread>
#include <QSignalBlocker>
#include <QSet>
#include <QPainter>
#include <QStyleOption>
#include <algorithm>
#include <exception>
namespace aster::ui
{
namespace
{
// Precision text changes during every read. Keep its geometry independent of
// the text so a narrow subplot cannot enter resize -> read -> resize loops.
class PlotResolutionLabel final : public QLabel
{
  public:
    PlotResolutionLabel()
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setTextFormat(Qt::PlainText);
    }
    QSize sizeHint() const override
    {
        const auto margins = contentsMargins();
        return {0, fontMetrics().height() + 2 * margin()
                  + margins.top() + margins.bottom()};
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QStyleOption option; option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
        const auto label = fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width());
        style()->drawItemText(&painter, contentsRect(), alignment(), palette(), isEnabled(), label, foregroundRole());
    }
};
}
PlotWorkspace::PlotWorkspace(QWidget *p) : QWidget(p)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget;
    m_tabs->setObjectName("plotPages");
    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    l->addWidget(m_tabs);
    m_empty = new PlotView(this);
    l->addWidget(m_empty);
    connect(m_tabs, &QTabWidget::currentChanged, this, &PlotWorkspace::activate);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &PlotWorkspace::closePage);
}
PlotWorkspace::~PlotWorkspace()
{
    m_cancel.cancel();
    if (m_worker)
    {
        m_worker->wait();
        delete m_worker;
    }
}
PlotView *PlotWorkspace::currentView() const
{
    if (m_active >= 0 && m_active < m_pages.size())
        for (const auto &c : m_pages[m_active].cells)
            if (c.id == m_selected && c.view)
                return c.view;
    return m_empty;
}
OutputReference PlotWorkspace::currentReference() const
{
    if (m_active >= 0 && m_active < m_pages.size())
        for (const auto &c : m_pages[m_active].cells)
            if (c.id == m_selected && !c.refs.isEmpty())
                return c.refs.front();
    return {};
}
bool PlotWorkspace::currentIsEmpty() const
{
    if (m_active >= 0 && m_active < m_pages.size())
        for (const auto &cell : m_pages[m_active].cells)
            if (cell.id == m_selected)
                return cell.refs.isEmpty() && cell.pendingRefs.isEmpty() && cell.view && !cell.view->hasOutput();
    return false;
}
bool PlotWorkspace::canOverlaySelection(const QVector<OutputReference> &refs) const
{
    if (loading() || refs.isEmpty()) return false;
    const bool empty = currentIsEmpty();
    if (!empty && (!currentView() || !currentView()->hasPlot() || !currentReference().valid())) return false;
    const auto groups = empty ? plotGroups(refs) : QVector<QVector<OutputReference>>();
    if (empty && groups.isEmpty()) return false;
    const auto target = empty ? groups.front().front() : currentReference();
    return std::all_of(refs.cbegin(), refs.cend(), [&](const OutputReference &ref) {
        return ref.valid() && canLayerReferences(target, ref);
    });
}
QVector<FigureCell> PlotWorkspace::figureCells(bool wholePage) const
{
    QVector<FigureCell> cells;
    if (m_active < 0)
        return cells;
    const auto &page = m_pages[m_active];
    const auto arrangement = page.host ? page.host->snapshot() : page.arrangement;
    const auto regions = arrangement.regions();
    for (auto id : arrangement.order())
    {
        const auto found = std::find_if(page.cells.cbegin(), page.cells.cend(),
                                       [id](const Cell &cell) { return cell.id == id; });
        if (found == page.cells.cend()) return {};
        const auto &c = *found;
        if (!wholePage && c.id != m_selected)
            continue;
        if (!c.view || !c.view->hasPlot())
            return {};
        auto style = c.view->styleWithViewport();
        // Freeze only the export snapshot. A different export resolution must
        // not recompute the visible colour scale or mutate the live view.
        if (dataAs<GridData>(c.view->currentData()) && style.colorUpper > style.colorLower)
            style.colorManual = true;
        cells.push_back({c.refs, style, c.view->plotRequest(), wholePage ? regions.value(c.id) : QRectF(0, 0, 1, 1)});
    }
    return cells;
}
int PlotWorkspace::pageCount() const
{
    return m_pages.size();
}
int PlotWorkspace::subplotCount() const
{
    return m_active >= 0 ? m_pages[m_active].cells.size() : 0;
}
QString PlotWorkspace::destinationLabel() const
{
    if (m_active < 0) return text("ui.plot_destination_empty");
    const auto order = m_pages[m_active].arrangement.order();
    for (int i = 0; i < order.size(); ++i)
        if (order[i] == m_selected)
        {
            const auto ref = currentReference();
            return text("ui.plot_destination").arg(m_active + 1).arg(i + 1) +
                (ref.valid() ? QString(" · ID %1 · %2").arg(ref.recordId).arg(portLabel(ref.port))
                             : QString(" · ") + text("ui.empty_subplot"));
        }
    return text("ui.plot_destination_page").arg(m_active + 1);
}
void PlotWorkspace::openBatch(const QVector<OutputReference> &references, Target target)
{
    QVector<OutputReference> refs;
    QSet<QString> keys;
    for (const auto &ref : references)
    {
        if (!ref.valid()) { emit message(text("ui.invalid_plot_selection")); return; }
        if (!keys.contains(ref.key())) { keys.insert(ref.key()); refs.push_back(ref); }
    }
    if (refs.isEmpty()) return;
    // Resolve missing hints from lightweight metadata, never by loading arrays.
    QMap<QString, QString> methods;
    for (auto &ref : refs)
        if (ref.method.isEmpty())
        {
            const auto key = ref.databasePath + ref.runId.toString() + ref.nodeId.toString();
            if (!methods.contains(key))
            {
                ResultRepository repo; const auto error = repo.open(ref.databasePath, false);
                if (!error.isEmpty()) { emit message(error); return; }
                const auto metadata = repo.loadPlotMetadata(ref);
                if (metadata.state != NodeState::Succeeded) { emit message(metadata.error); return; }
                methods[key] = metadata.report["plot_method"].toString();
            }
            ref.method = methods[key];
        }
    emit message({});
    if (target == Target::Overlay || target == Target::Replace)
    {
        if (m_active < 0 || (target == Target::Replace && refs.size() != 1))
        { emit message(text("ui.select_plot_destination")); return; }
        for (auto &c : m_pages[m_active].cells)
            if (c.id == m_selected)
            {
                const bool empty = c.refs.isEmpty();
                if (loading() || !c.view || (!empty && !c.view->hasOutput()))
                { emit message(text("ui.wait_for_plots_to_load_use_csv_for_tables")); return; }
                auto candidate = target == Target::Overlay ? c.refs : QVector<OutputReference>();
                // A cleared slot has no axes. Recipes choose its first axis
                // owner (e.g. wavelet power before COI, spectrum before peaks).
                QVector<OutputReference> ordered;
                if (empty)
                    for (const auto &group : plotGroups(refs)) ordered += group;
                else ordered = refs;
                for (const auto &ref : ordered)
                    if (std::none_of(candidate.cbegin(), candidate.cend(), [&ref](const OutputReference &v) { return v.key() == ref.key(); }))
                        candidate.push_back(ref);
                if (target == Target::Overlay)
                {
                    if (candidate.size() > PlotLimits::maxFamilyOutputs || plotGroups(candidate).size() > 8)
                    { emit message(text("error.at_most_eight_layers_per_subplot")); return; }
                    if (std::any_of(candidate.cbegin(), candidate.cend(), [&](const OutputReference &v) { return !canLayerReferences(candidate.front(), v); }))
                    { emit message(text("ui.overlay_compatibility_help")); return; }
                    if (candidate.size() == c.refs.size()) return;
                }
                store();
                c.pendingRefs = candidate;
                c.replacing = target == Target::Replace || empty;
                queue(c); pump();
                return;
            }
        return;
    }
    // Four cells per page remain the memory/rendering bound. Overflow goes to
    // new pages; their numeric buffers are loaded only when activated.
    const auto groups = plotGroups(refs, target == Target::Separate);
    const int available = target == Target::Page || m_active < 0 ? 0 : 4 - subplotCount();
    const int neededPages = (qMax(0, int(groups.size()) - available) + 3) / 4;
    if (m_pages.size() + neededPages > 32)
    { emit message(text("ui.close_a_page_before_adding_more_limit_32")); return; }
    bool first = true;
    for (const auto &group : groups)
    {
        openReferences(group, (first && target == Target::Page) || m_active < 0 || subplotCount() >= 4
                      ? Target::Page : Target::Subplot);
        first = false;
    }
}
void PlotWorkspace::store()
{
    if (m_active < 0 || m_active >= m_pages.size())
        return;
    auto &page = m_pages[m_active];
    if (page.host) page.arrangement = page.host->snapshot();
    for (auto &c : page.cells)
        if (c.view && c.view->hasOutput())
        {
            c.style = c.view->styleForReload();
            c.saved = true;
        }
}
void PlotWorkspace::activate(int index)
{
    if (index == m_active)
        return;
    store();
    emit currentViewChanged(nullptr);
    m_cancel.cancel();
    m_jobs.clear();
    if (m_active >= 0 && m_active < m_pages.size())
    {
        auto *w = m_pages[m_active].widget.data();
        // Delete contents, keep tab identity. Inactive pages contain no numeric buffers.
        if (w && w->layout())
        {
            while (auto *item = w->layout()->takeAt(0))
            {
                delete item->widget();
                delete item;
            }
        }
        for (auto &c : m_pages[m_active].cells)
        {
            c.view = nullptr;
            ++c.generation;
        }
    }
    m_active = index;
    m_selected = {};
    build();
}
void PlotWorkspace::build()
{
    m_empty->setVisible(m_active < 0);
    m_tabs->setVisible(m_active >= 0);
    if (m_active < 0)
    {
        emit currentViewChanged(m_empty);
        emit selectionChanged({});
        return;
    }
    auto &page = m_pages[m_active];
    if (!page.widget->layout())
    {
        auto *layout = new QVBoxLayout(page.widget);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(0);
    }
    if (!page.host)
    {
        page.host = new PlotLayoutWidget;
        page.widget->layout()->addWidget(page.host);
        connect(page.host, &PlotLayoutWidget::selectionRequested, this, &PlotWorkspace::select);
        const auto pageId = page.id;
        connect(page.host, &PlotLayoutWidget::layoutChanged, this, [this, pageId](bool custom) {
            for (auto &p : m_pages)
                if (p.id == pageId && p.host)
                {
                    p.arrangement = p.host->snapshot();
                    p.customArrangement = custom;
                    emit selectionChanged(currentReference());
                    break;
                }
        });
    }
    for (int i = 0; i < page.cells.size(); ++i)
    {
        auto &c = page.cells[i];
        if (c.view)
            continue;
        auto *panel = new QWidget;
        auto *vl = new QVBoxLayout(panel);
        vl->setContentsMargins(3, 3, 3, 3);
        auto *button = page.host->createTitle(c.id,
            c.refs.isEmpty()
                ? text("ui.empty_subplot")
                : defaultPlotTitle(c.refs[0].recordId, c.refs[0].port));
        button->setObjectName("selectSubplot");
        button->setCheckable(true);
        button->setChecked(c.id == m_selected);
        vl->addWidget(button);
        c.view = new PlotView;
        c.view->setMinimumSize(200, 130);
        // A dense legend must not spill over axis labels or the next panel.
        // Let the existing page scroll when the complete legend cannot fit;
        // keep the user's fonts and all legend entries unchanged.
        const auto fitLegend = [view = c.view] {
            QTimer::singleShot(0, view, [view] {
                auto *plot = view->plotWidget();
                QSize minimum(200, 130);
                if (view->hasPlot() && plot->legend->visible())
                {
                    const auto legend = plot->legend->minimumOuterSizeHint();
                    const auto axis = plot->axisRect()->rect();
                    const int outsideX = qBound(60, view->width() - axis.width(), 400);
                    const int outsideY = qBound(60, view->height() - axis.height(), 300);
                    minimum = minimum.expandedTo(legend + QSize(outsideX + 16, outsideY + 16));
                }
                if (view->minimumSize() != minimum) view->setMinimumSize(minimum);
            });
        };
        connect(c.view, &PlotView::outputChanged, c.view, fitLegend);
        connect(c.view, &PlotView::styleChanged, c.view, fitLegend);
        // Axis margins settle during replot and can change after a resize.
        connect(c.view->plotWidget(), &QCustomPlot::afterReplot, c.view, fitLegend);
        vl->addWidget(c.view, 1);
        auto *resolution = new PlotResolutionLabel;
        resolution->setObjectName("plotResolution");
        resolution->setProperty("role", "muted");
        vl->addWidget(resolution);
        auto id = c.id;
        connect(c.view, &PlotView::clearedByUser, this, [this, id] { clearCell(id); });
        connect(c.view, &PlotView::activated, this, [this, id] { select(id); });
        connect(c.view, &PlotView::viewportChanged, this,
                [this, id]
                {
                    if (m_active < 0)
                        return;
                    for (auto &cell : m_pages[m_active].cells)
                        if (cell.id == id && cell.view && cell.view->hasPlot())
                        {
                            queue(cell, true);
                            pump();
                            break;
                        }
                });
        if (m_selected.isNull())
            m_selected = id;
        if (c.refs.isEmpty() && c.pendingRefs.isEmpty()) showEmpty(c);
        queue(c);
    }
    reflow(page);
    select(m_selected);
    pump();
}
void PlotWorkspace::reflow(Page &page)
{
    QVector<QUuid> ids;
    QMap<QUuid, QWidget *> panels;
    for (const auto &cell : page.cells)
    {
        ids.push_back(cell.id);
        if (cell.view) panels[cell.id] = cell.view->parentWidget();
    }
    if (!page.customArrangement) page.arrangement = PlotArrangement::defaults(ids);
    else
    {
        for (auto id : page.arrangement.order())
            if (!ids.contains(id)) page.arrangement.remove(id);
        for (auto id : ids)
            if (!page.arrangement.find(id)) page.arrangement.append(id);
    }
    if (page.host) page.host->arrange(page.arrangement, panels, ids);
}
void PlotWorkspace::select(QUuid id)
{
    if (m_active < 0)
        return;
    m_selected = id;
    for (auto &c : m_pages[m_active].cells)
        if (c.view)
        {
            auto *panel = c.view->parentWidget();
            if (auto *button = panel->findChild<QPushButton *>("selectSubplot"))
                button->setChecked(c.id == id);
            c.view->setProperty("selectedSubplot", c.id == id);
        }
    emit currentViewChanged(currentView());
    emit selectionChanged(currentReference());
}
void PlotWorkspace::queue(Cell &c, bool refine)
{
    if (refine && !c.pendingRefs.isEmpty()) return;
    // The bounded cross-spectrum table is already complete; zoom needs no I/O.
    if (refine && !c.refs.isEmpty() && c.refs.front().port == "cross_spectrum") return;
    if (c.refs.isEmpty() && c.pendingRefs.isEmpty())
        return;
    ++c.generation;
    if (m_worker && m_workerCell == c.id)
        m_cancel.cancel();
    if (!refine && (c.pendingRefs.isEmpty() || !c.view->hasOutput()))
        c.view->clear(text("ui.loading_and_preparing_plot"));
    for (int i = m_jobs.size() - 1; i >= 0; --i)
        if (m_jobs[i].cell == c.id)
            m_jobs.removeAt(i);
    auto request = c.view->plotRequest();
    if (!c.view->hasPlot()) { request.columns = 1200; request.rows = 600; }
    request.window = refine;
    if (auto *status = c.view->parentWidget()->findChild<QLabel *>("plotResolution"))
        status->setText(text(refine ? "ui.plot_refining" : "ui.loading_and_preparing_plot"));
    const bool transaction = !c.pendingRefs.isEmpty();
    m_jobs.push_back({m_pages[m_active].id, c.id, c.generation,
                     transaction ? c.pendingRefs : c.refs, request, refine,
                     transaction, c.replacing});
}
void PlotWorkspace::open(const OutputReference &ref, Target target)
{
    openReferences({ref}, target);
}
void PlotWorkspace::openReferences(const QVector<OutputReference> &references, Target target)
{
    if (references.isEmpty()) return;
    const auto &ref = references.front();
    if (!ref.valid())
        return;
    if (target == Target::Overlay || target == Target::Replace)
    { openBatch({ref}, target); return; }
    if (target == Target::Current)
    {
        // Each record gets a default page. Returning to an ID restores that page.
        int found = -1;
        if (m_active >= 0 && !m_pages[m_active].cells.isEmpty() &&
            !m_pages[m_active].cells[0].refs.isEmpty())
        {
            const auto &current = m_pages[m_active].cells[0].refs[0];
            if (current.runId == ref.runId && current.databasePath == ref.databasePath)
                found = m_active;
        }
        for (int i = 0; found < 0 && i < m_pages.size(); ++i)
            if (!m_pages[i].cells.isEmpty() && !m_pages[i].cells[0].refs.isEmpty() &&
                m_pages[i].cells[0].refs[0].runId == ref.runId &&
                m_pages[i].cells[0].refs[0].databasePath == ref.databasePath)
            {
                found = i;
                break;
            }
        if (found >= 0 && found != m_active)
            m_tabs->setCurrentIndex(found);
        if (found < 0)
            target = Target::Page;
    }
    if (m_active < 0 || target == Target::Page)
    {
        if (m_pages.size() >= 32)
        {
            emit message(text("ui.close_a_page_before_adding_more_limit_32"));
            return;
        }
        store();
        Page page;
        page.label = text("ui.figure_page").arg(m_pages.size() + 1);
        page.widget = new QWidget;
        {
            QSignalBlocker b(m_tabs);
            m_pages.push_back(page);
            m_tabs->addTab(page.widget, page.label);
        }
        m_tabs->setCurrentIndex(m_pages.size() - 1);
        if (m_active != m_pages.size() - 1)
            activate(m_pages.size() - 1);
    }
    store();
    auto &page = m_pages[m_active];
    if (page.cells.isEmpty() || target == Target::Subplot)
    {
        if (page.cells.size() >= 4)
        {
            emit message(text("ui.four_subplots_per_page_create_another_page"));
            return;
        }
        Cell c;
        c.refs = references;
        m_selected = c.id;
        page.cells.push_back(c);
        build();
        return;
    }
    for (auto &c : page.cells)
        if (c.id == m_selected)
        {
            if (references.size() == 1 && c.refs.size() == 1 && c.refs[0].key() == ref.key() &&
                (c.view->hasOutput() || loading()))
                return;
            c.pendingRefs.clear();
            c.refs = references;
            c.saved = false;
            queue(c);
            select(c.id);
            pump();
            return;
        }
}
void PlotWorkspace::openDefault(const OutputReference &ref)
{
    if (!ref.valid()) return;
    emit message({});
    // Prefer the visible page, then restore a matching existing page. Only
    // primary references match: an overlay member is not a separate default plot.
    QVector<int> order;
    if (m_active >= 0) order.push_back(m_active);
    for (int i = 0; i < m_pages.size(); ++i)
        if (i != m_active) order.push_back(i);
    for (int index : order)
        for (const auto &cell : m_pages[index].cells)
            if (!cell.refs.isEmpty() && cell.refs.front().key() == ref.key())
            {
                const auto id = cell.id;
                if (index != m_active) m_tabs->setCurrentIndex(index);
                select(id);
                return;
            }
    open(ref, m_defaultPlotNewPage || m_active < 0 || subplotCount() >= 4 ? Target::Page : Target::Subplot);
}
void PlotWorkspace::pump()
{
    if (m_worker || m_jobs.isEmpty())
        return;
    const auto job = m_jobs.takeFirst();
    m_workerCell = job.cell;
    m_cancel = CancellationToken();
    auto cancel = m_cancel;
    auto result = std::make_shared<NodeResult>();
    m_worker = QThread::create(
        [job, result, cancel]
        {
            try
            {
                *result = loadPlotData(job.refs, job.request, cancel);
            }
            catch (const std::exception &e)
            {
                *result = NodeResult::failure(QString::fromUtf8(e.what()));
            }
            catch (...)
            {
                *result = NodeResult::failure("Unexpected plot preparation error");
            }
        });
    connect(m_worker, &QThread::finished, this,
            [this, job, result, cancel]
            {
                auto *w = m_worker;
                m_worker = nullptr;
                w->deleteLater();
                if (!cancel.isCancelled() && m_active >= 0 && m_pages[m_active].id == job.page)
                    for (auto &c : m_pages[m_active].cells)
                        if (c.id == job.cell && c.generation == job.generation && c.view)
                        {
                            if (result->state == NodeState::Succeeded)
                            {
                                if (job.transaction)
                                {
                                    if (!job.replacing && c.view->hasOutput())
                                        c.style = c.view->styleForReload();
                                    if (!job.replacing)
                                        for (const auto &ref : job.refs)
                                            if (ref.nodeId == c.refs.front().nodeId && ref.runId == c.refs.front().runId &&
                                                ref.databasePath == c.refs.front().databasePath && c.style.curves.contains(ref.port) &&
                                                std::none_of(c.refs.cbegin(), c.refs.cend(), [&ref](const OutputReference &v) { return v.key() == ref.key(); }))
                                                c.style.curves[ref.port].visible = true;
                                    c.refs = job.refs;
                                    c.pendingRefs.clear();
                                    if (job.replacing) c.saved = false;
                                }
                                if (job.refine)
                                    c.view->refreshOutput(*result);
                                else
                                    c.view->showOutput(*result, c.refs[0].port, c.refs[0].key());
                                c.refs[0].method = result->report["plot_method"].toString(c.refs[0].method);
                                c.view->setOutputReference(c.refs[0]);
                                if (auto *label = c.view->parentWidget()->findChild<QPushButton *>(
                                        "selectSubplot"))
                                {
                                    label->setText(defaultPlotTitle(c.refs[0].recordId, c.refs[0].port));
                                    label->setToolTip(text("ui.plot_title_drag_help") + "\n" + c.view->resolutionDescription());
                                }
                                if (auto *status = c.view->parentWidget()->findChild<QLabel *>("plotResolution"))
                                {
                                    status->setText(c.view->resolutionDescription());
                                    status->setToolTip(c.view->resolutionDescription() + "\n" + text("ui.plot_sampling_details"));
                                }
                                if (c.saved && !job.refine)
                                {
                                    auto style = c.view->style();
                                    auto old = c.style;
                                    for (auto it = style.curves.begin(); it != style.curves.end();
                                         ++it)
                                        if (old.curves.contains(it.key()))
                                        {
                                            auto saved = old.curves[it.key()];
                                            if (old.followTheme &&
                                                saved.color == saved.themeDefaultColor)
                                                saved.color = it->color;
                                            saved.themeDefaultColor = it->themeDefaultColor;
                                            it.value() = saved;
                                        }
                                    old.curves = style.curves;
                                    if (old.followTheme)
                                    {
                                        old.background = themeColor("plot");
                                        for (auto *a : {&old.x, &old.y})
                                        {
                                            a->axisColor = themeColor("axis");
                                            a->gridColor = themeColor("grid");
                                            a->subGridColor = themeColor("subGrid");
                                        }
                                    }
                                    // Start automatic axes from the combined visible data.
                                    // Reapply only ranges explicitly zoomed or entered by the user.
                                    c.view->applyStyle(old);
                                    c.view->fitAutomaticAxes();
                                }
                                if (!job.refine) c.view->scheduleRefinement();
                            }
                            else
                            {
                                const auto diagnostic = result->diagnostic();
                                if (diagnostic.code == "PLOT_OVERLAY_AXES")
                                {
                                    const auto context = diagnostic.context;
                                    const auto unit = [](const QJsonValue &value) {
                                        return value.toString().isEmpty() ? text("ui.unit_unspecified") : value.toString();
                                    };
                                    emit message(text("ui.overlay_incompatible_detail")
                                        .arg(context["target_record"].toString(), context["input_record"].toString(),
                                             unit(context["target_x"]), unit(context["target_y"]),
                                             unit(context["input_x"]), unit(context["input_y"])));
                                }
                                else emit message(result->error);
                                if (auto *status = c.view->parentWidget()->findChild<QLabel *>("plotResolution"))
                                    status->setText(c.view->resolutionDescription());
                                if (job.transaction)
                                {
                                    c.pendingRefs.clear(); // Original figure/style/reference remain intact.
                                    // A page may have been unloaded while validating the candidate.
                                    // Returning to it must restore the original figure on rejection.
                                    if (c.refs.isEmpty()) showEmpty(c);
                                    else if (!c.view->hasOutput()) queue(c);
                                    // Gestures are deferred while a candidate is being validated.
                                    // A rejected candidate must release that pending viewport too.
                                    else if (c.view->hasPlot()) c.view->scheduleRefinement();
                                }
                                else if (!job.refine)
                                    c.view->clear(result->error);
                            }
                            if (c.id == m_selected && !job.refine)
                                emit currentViewChanged(c.view);
                            if (job.transaction && c.id == m_selected)
                                emit selectionChanged(currentReference());
                            break;
                        }
                pump();
                // Also notify after a cancelled/removed job drains: controls
                // that depend on loading() must not remain disabled.
                emit outputReady();
            });
    m_worker->start();
}
void PlotWorkspace::invalidate(Cell &cell)
{
    ++cell.generation;
    if (m_worker && m_workerCell == cell.id) m_cancel.cancel();
    for (int i = m_jobs.size() - 1; i >= 0; --i)
        if (m_jobs[i].cell == cell.id) m_jobs.removeAt(i);
}
void PlotWorkspace::showEmpty(Cell &cell)
{
    if (!cell.view) return;
    cell.view->clear(text("ui.empty_subplot_help"));
    if (auto *title = cell.view->parentWidget()->findChild<QPushButton *>("selectSubplot"))
    {
        title->setText(text("ui.empty_subplot"));
        title->setToolTip(text("ui.plot_title_drag_help"));
    }
    if (auto *status = cell.view->parentWidget()->findChild<QLabel *>("plotResolution"))
    { status->clear(); status->setToolTip({}); }
}
void PlotWorkspace::clearCell(QUuid id)
{
    if (m_active < 0) return;
    for (auto &cell : m_pages[m_active].cells)
        if (cell.id == id)
        {
            invalidate(cell); // Late reads and pending overlays must not revive the plot.
            cell.refs.clear(); cell.pendingRefs.clear();
            cell.replacing = false; cell.saved = false; cell.style = {};
            showEmpty(cell);
            select(id);
            pump();
            emit outputReady();
            return;
        }
}
void PlotWorkspace::closeSubplot()
{
    if (m_active < 0)
        return;
    auto &page = m_pages[m_active];
    if (page.cells.size() == 1)
    {
        closePage(m_active);
        return;
    }
    store();
    emit currentViewChanged(nullptr);
    for (int i = 0; i < page.cells.size(); ++i)
        if (page.cells[i].id == m_selected)
        {
            invalidate(page.cells[i]);
            delete page.cells[i].view->parentWidget();
            page.cells.removeAt(i);
            break;
        }
    reflow(page);
    select(page.arrangement.order().front());
}
void PlotWorkspace::closePage(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
    if (index != m_active)
    {
        // A background tab owns no visible buffers. Removing it must not
        // rebuild the active page or change its selected subplot.
        for (auto &cell : m_pages[index].cells) invalidate(cell);
        auto *widget = m_pages[index].widget.data();
        {
            QSignalBlocker block(m_tabs);
            m_tabs->removeTab(index);
            m_pages.removeAt(index);
            if (index < m_active) --m_active;
            for (int i = 0; i < m_pages.size(); ++i)
            {
                m_pages[i].label = text("ui.figure_page").arg(i + 1);
                m_tabs->setTabText(i, m_pages[i].label);
            }
        }
        delete widget;
        emit selectionChanged(currentReference());
        return;
    }
    store();
    emit currentViewChanged(nullptr);
    m_cancel.cancel();
    m_jobs.clear();
    // Unload all current content before indices shift.
    if (m_active >= 0)
    {
        auto *w = m_pages[m_active].widget.data();
        if (w->layout())
            while (auto *item = w->layout()->takeAt(0))
            {
                delete item->widget();
                delete item;
            }
        for (auto &c : m_pages[m_active].cells)
        {
            c.view = nullptr;
            ++c.generation;
        }
    }
    auto *widget = m_pages[index].widget.data();
    {
        QSignalBlocker b(m_tabs);
        m_tabs->removeTab(index);
        m_pages.removeAt(index);
        for (int i = 0; i < m_pages.size(); ++i)
        {
            m_pages[i].label = text("ui.figure_page").arg(i + 1);
            m_tabs->setTabText(i, m_pages[i].label);
        }
    }
    delete widget;
    m_active = -1;
    if (m_pages.isEmpty())
        build();
    else
    {
        int next = std::min(index, int(m_pages.size()) - 1);
        m_tabs->setCurrentIndex(next);
        if (m_active != next)
            activate(next);
    }
}
void PlotWorkspace::forgetRecord(const QString &path, qint64 recordId)
{
    const auto matches = [&path, recordId](const OutputReference &ref) {
        return ref.databasePath == path && ref.recordId == recordId;
    };
    for (int i = m_pages.size() - 1; i >= 0; --i)
    {
        int affected = 0;
        for (const auto &cell : m_pages[i].cells)
            if (std::any_of(cell.refs.cbegin(), cell.refs.cend(), matches)) ++affected;
        if (affected && affected == m_pages[i].cells.size())
        {
            closePage(i);
            continue;
        }
        if (affected && i == m_active) store();
        auto &page = m_pages[i];
        for (int j = page.cells.size() - 1; j >= 0; --j)
        {
            auto &cell = page.cells[j];
            if (std::any_of(cell.refs.cbegin(), cell.refs.cend(), matches))
            {
                if (cell.id == m_selected) emit currentViewChanged(nullptr);
                invalidate(cell);
                if (cell.view) delete cell.view->parentWidget();
                page.cells.removeAt(j);
            }
            else if (std::any_of(cell.pendingRefs.cbegin(), cell.pendingRefs.cend(), matches))
            {
                // Discard the whole candidate, preserving the committed plot.
                // Generation/cancellation also reject an already-finished I/O
                // snapshot whose completion is still queued on the GUI thread.
                invalidate(cell);
                cell.pendingRefs.clear();
                cell.replacing = false;
                if (i == m_active && cell.view)
                {
                    if (cell.refs.isEmpty()) showEmpty(cell);
                    else if (!cell.view->hasOutput()) queue(cell);
                    else if (cell.view->hasPlot()) cell.view->scheduleRefinement();
                }
            }
        }
        if (affected && i != m_active) reflow(page);
        if (affected && i == m_active)
        {
            // A page can contain independent records. Reflow surviving widgets
            // without unloading their data or losing their styles/selection.
            reflow(page);
            const bool keptSelection = std::any_of(page.cells.cbegin(), page.cells.cend(),
                [this](const Cell &cell) { return cell.id == m_selected; });
            select(keptSelection ? m_selected : page.cells.front().id);
        }
    }
    pump();
    emit selectionChanged(currentReference());
}
void PlotWorkspace::reset()
{
    while (!m_pages.isEmpty())
        closePage(m_pages.size() - 1);
    m_empty->clear();
}
} // namespace aster::ui
