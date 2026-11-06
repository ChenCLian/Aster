// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/services/plot_data_loader.h"
#include "ui/settings/theme_manager.h"
#include "ui/results/plot_workspace.h"
#include "io/database/result_repository.h"
#include "ui/common/presentation.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QThread>
#include <QSignalBlocker>
#include <algorithm>
#include <exception>
namespace aster::ui
{
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
QVector<FigureCell> PlotWorkspace::figureCells(bool wholePage) const
{
    QVector<FigureCell> cells;
    if (m_active < 0)
        return cells;
    for (const auto &c : m_pages[m_active].cells)
    {
        if (!wholePage && c.id != m_selected)
            continue;
        if (!c.view || !c.view->hasPlot())
            return {};
        cells.push_back({c.refs, c.view->styleWithViewport(), c.view->plotRequest()});
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
void PlotWorkspace::store()
{
    if (m_active < 0 || m_active >= m_pages.size())
        return;
    for (auto &c : m_pages[m_active].cells)
        if (c.view && c.view->hasOutput())
        {
            c.style = c.view->styleWithViewport();
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
        return;
    }
    auto &page = m_pages[m_active];
    auto *layout = qobject_cast<QGridLayout *>(page.widget->layout());
    if (!layout)
    {
        layout = new QGridLayout(page.widget);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(6);
    }
    for (int i = 0; i < page.cells.size(); ++i)
    {
        auto &c = page.cells[i];
        if (c.view)
            continue;
        auto *panel = new QWidget;
        auto *vl = new QVBoxLayout(panel);
        vl->setContentsMargins(3, 3, 3, 3);
        auto *button = new QPushButton(
            c.refs.isEmpty()
                ? text("ui.empty_subplot")
                : QString("ID %1 · %2").arg(c.refs[0].recordId).arg(portLabel(c.refs[0].port)));
        button->setObjectName("selectSubplot");
        button->setCheckable(true);
        button->setChecked(c.id == m_selected);
        vl->addWidget(button);
        c.view = new PlotView;
        c.view->setMinimumSize(200, 130);
        vl->addWidget(c.view, 1);
        layout->addWidget(panel, i / 2, i % 2);
        auto id = c.id;
        connect(button, &QPushButton::clicked, this, [this, id] { select(id); });
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
        queue(c);
    }
    select(m_selected);
    pump();
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
    if (c.refs.isEmpty())
        return;
    ++c.generation;
    if (m_worker && m_workerCell == c.id)
        m_cancel.cancel();
    if (!refine)
        c.view->clear(text("ui.loading_and_preparing_plot"));
    for (int i = m_jobs.size() - 1; i >= 0; --i)
        if (m_jobs[i].cell == c.id)
            m_jobs.removeAt(i);
    auto request = c.view->plotRequest();
    request.window = refine;
    m_jobs.push_back({m_pages[m_active].id, c.id, c.generation, c.refs, request, refine});
}
void PlotWorkspace::open(const OutputReference &ref, Target target)
{
    if (!ref.valid())
        return;
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
        page.label = QString("ID %1").arg(ref.recordId);
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
        c.refs = {ref};
        m_selected = c.id;
        page.cells.push_back(c);
        build();
        return;
    }
    for (auto &c : page.cells)
        if (c.id == m_selected)
        {
            if (target == Target::Overlay)
            {
                if (c.refs.size() >= 8)
                {
                    emit message(text("error.at_most_eight_layers_per_subplot"));
                    return;
                }
                for (const auto &existing : c.refs)
                    if (existing.key() == ref.key())
                        return;
                c.refs.push_back(ref);
            }
            else
            {
                if (c.refs.size() == 1 && c.refs[0].key() == ref.key() &&
                    (c.view->hasOutput() || loading()))
                    return;
                c.refs = {ref};
                c.saved = false;
            }
            queue(c);
            select(c.id);
            pump();
            return;
        }
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
                                if (job.refine)
                                    c.view->refreshOutput(*result);
                                else
                                    c.view->showOutput(*result, c.refs[0].port, c.refs[0].key());
                                c.view->setOutputReference(c.refs[0]);
                                if (auto *label = c.view->parentWidget()->findChild<QPushButton *>(
                                        "selectSubplot"))
                                {
                                    label->setText(QString("ID %1 · %2")
                                                       .arg(c.refs[0].recordId)
                                                       .arg(portLabel(c.refs[0].port)) +
                                                   (result->report["display_reduced"].toBool()
                                                        ? text("ui.reduced_preview")
                                                        : QString()));
                                    label->setToolTip(text("ui.curves_retain_bucket_extrema_large_"
                                                           "heat_maps_preview_maximum"));
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
                                    c.view->applyStyle(old);
                                }
                            }
                            else
                            {
                                emit message(result->error);
                                if (c.refs.size() > 1)
                                {
                                    c.refs.removeLast();
                                    queue(c);
                                }
                                else if (!job.refine)
                                    c.view->clear(result->error);
                            }
                            if (c.id == m_selected && !job.refine)
                                emit currentViewChanged(c.view);
                            emit outputReady();
                            break;
                        }
                pump();
            });
    m_worker->start();
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
            delete page.cells[i].view->parentWidget();
            page.cells.removeAt(i);
            break;
        }
    // Reflow existing widgets without losing plots.
    auto *layout = qobject_cast<QGridLayout *>(page.widget->layout());
    while (auto *item = layout->takeAt(0))
        delete item;
    for (int i = 0; i < page.cells.size(); ++i)
        layout->addWidget(page.cells[i].view->parentWidget(), i / 2, i % 2);
    select(page.cells[0].id);
}
void PlotWorkspace::closePage(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
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
    for (int i = m_pages.size() - 1; i >= 0; --i)
    {
        bool affected = false;
        for (const auto &cell : m_pages[i].cells)
            for (const auto &ref : cell.refs)
                if (ref.databasePath == path && ref.recordId == recordId)
                    affected = true;
        if (affected)
            closePage(i);
    }
}
void PlotWorkspace::reset()
{
    while (!m_pages.isEmpty())
        closePage(m_pages.size() - 1);
    m_empty->clear();
}
} // namespace aster::ui
