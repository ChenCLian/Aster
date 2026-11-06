// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/results/contour_plottable.h"
namespace aster::ui
{
ContourPlottable::ContourPlottable(QCPAxis *x, QCPAxis *y, QVector<QLineF> segments)
    : QCPAbstractPlottable(x,y), m_segments(std::move(segments))
{
    setObjectName("significanceContour");
    setSelectable(QCP::stNone);
    removeFromLegend();
}
// This overlay must not extend the heatmap's data range or alter autoscaling.
QCPRange ContourPlottable::getKeyRange(bool &found, QCP::SignDomain) const { found=false; return {}; }
QCPRange ContourPlottable::getValueRange(bool &found, QCP::SignDomain, const QCPRange &) const { found=false; return {}; }
void ContourPlottable::draw(QCPPainter *p)
{
    if (!mKeyAxis || !mValueAxis) return;
    QVector<QLineF> lines;
    lines.reserve(m_segments.size());
    for (const auto &s : m_segments)
        lines.push_back(QLineF(coordsToPixels(s.x1(),s.y1()), coordsToPixels(s.x2(),s.y2())));
    p->save();
    p->setClipRect(clipRect(), Qt::IntersectClip);
    // Thin contrasting halo keeps the scientific outline legible on dark palettes.
    QPen halo(QColor(255,255,255,190),mPen.widthF()+1.2,Qt::SolidLine,Qt::RoundCap);
    p->setPen(halo); p->drawLines(lines);
    auto stroke=mPen; stroke.setCapStyle(Qt::RoundCap);
    p->setPen(stroke); p->drawLines(lines);
    p->restore();
}
void ContourPlottable::drawLegendIcon(QCPPainter *p, const QRectF &rect) const
{ p->setPen(mPen); p->drawLine(QLineF(rect.left(),rect.center().y(),rect.right(),rect.center().y())); }
} // namespace aster::ui
