// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_PLOT_AXIS_HINTS_H
#define ASTER_UI_PLOT_AXIS_HINTS_H
#include "ui/common/presentation.h"
#include "third_party/qcustomplot/qcustomplot.h"
#include <QHelpEvent>
#include <QToolTip>

namespace aster::ui
{
// Qt's delayed help event keeps normal navigation free of additional controls.
class PlotAxisHints final : public QObject
{
  public:
    explicit PlotAxisHints(QCustomPlot *plot) : QObject(plot), m_plot(plot)
    {
        plot->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object != m_plot) return false;
        if (event->type() == QEvent::ToolTip)
        {
            const auto *help = static_cast<QHelpEvent *>(event);
            for (auto *axis : {m_plot->xAxis, m_plot->yAxis})
            {
                const auto orientation = axis->orientation();
                auto *rect = axis->axisRect();
                if (axis->visible() && axis->selectTest(help->pos(), false) >= 0 &&
                    m_plot->interactions().testFlag(QCP::iRangeZoom) &&
                    m_plot->interactions().testFlag(QCP::iRangeDrag) &&
                    rect->rangeZoom().testFlag(orientation) &&
                    rect->rangeDrag().testFlag(orientation) &&
                    rect->rangeZoomAxes(orientation).contains(axis) &&
                    rect->rangeDragAxes(orientation).contains(axis))
                {
                    QToolTip::showText(help->globalPos(),
                                      text(axis == m_plot->xAxis ? "ui.axis_hover_x" : "ui.axis_hover_y"),
                                      m_plot, {}, 5000);
                    m_hintShown = true;
                    event->accept();
                    return true;
                }
            }
            hideHint();
        }
        else if (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave ||
                 event->type() == QEvent::Hide || event->type() == QEvent::Resize ||
                 event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Wheel)
            hideHint();
        return false; // QCustomPlot owns all dragging and wheel behavior.
    }

  private:
    void hideHint()
    {
        if (m_hintShown) QToolTip::hideText();
        m_hintShown = false;
    }
    QCustomPlot *m_plot;
    bool m_hintShown = false;
};
}
#endif
