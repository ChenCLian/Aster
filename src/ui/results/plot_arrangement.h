// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PLOT_ARRANGEMENT_H
#define ASTER_PLOT_ARRANGEMENT_H
#include <QUuid>
#include <QRectF>
#include <QMap>
#include <QSet>
#include <QVector>
#include <vector>
#include <cmath>
#include <algorithm>
namespace aster::ui
{
enum class PlotPlacement { Swap, Left, Right, Above, Below };

// Small value tree: leaves refer to stable subplot identities, never data or
// widgets. The same geometry is used for display, drop previews and export.
struct PlotArrangement
{
    QUuid cell;
    Qt::Orientation orientation = Qt::Horizontal;
    double fraction = .5;
    std::vector<PlotArrangement> children;

    bool empty() const { return cell.isNull() && children.empty(); }
    QVector<QUuid> order() const
    {
        if (!cell.isNull()) return {cell};
        QVector<QUuid> ids;
        for (const auto &child : children) ids += child.order();
        return ids;
    }
    static PlotArrangement leaf(QUuid id) { PlotArrangement n; n.cell = id; return n; }
    static PlotArrangement split(Qt::Orientation axis, PlotArrangement a, PlotArrangement b)
    {
        PlotArrangement n; n.orientation = axis; n.children = {std::move(a), std::move(b)}; return n;
    }
    static PlotArrangement defaults(const QVector<QUuid> &ids)
    {
        if (ids.isEmpty() || ids.size() > 4) return {};
        if (ids.size() == 1) return leaf(ids[0]);
        auto top = split(Qt::Horizontal, leaf(ids[0]), leaf(ids[1]));
        if (ids.size() == 2) return top;
        auto bottom = ids.size() == 3 ? leaf(ids[2]) : split(Qt::Horizontal, leaf(ids[2]), leaf(ids[3]));
        return split(Qt::Vertical, std::move(top), std::move(bottom));
    }
    bool valid(bool allowEmpty = false) const
    {
        QSet<QUuid> seen;
        const auto visit = [&](const auto &self, const PlotArrangement &node) -> bool {
            if (!node.cell.isNull())
            {
                if (!node.children.empty() || seen.contains(node.cell)) return false;
                seen.insert(node.cell); return true;
            }
            return node.children.size() == 2 && std::isfinite(node.fraction) &&
                node.fraction > 0 && node.fraction < 1 &&
                (node.orientation == Qt::Horizontal || node.orientation == Qt::Vertical) &&
                self(self, node.children[0]) && self(self, node.children[1]);
        };
        return (allowEmpty && empty()) || (visit(visit, *this) && seen.size() <= 4);
    }
    PlotArrangement *find(QUuid id)
    {
        if (!id.isNull() && cell == id) return this;
        for (auto &child : children) if (auto *found = child.find(id)) return found;
        return nullptr;
    }
    QMap<QUuid, QRectF> regions(QRectF area = QRectF(0, 0, 1, 1)) const
    {
        if (!valid()) return {};
        QMap<QUuid, QRectF> result;
        const auto visit = [&](const auto &self, const PlotArrangement &node, QRectF box) -> void {
            if (!node.cell.isNull()) { result[node.cell] = box; return; }
            auto a = box, b = box;
            if (node.orientation == Qt::Horizontal)
            { a.setWidth(box.width() * node.fraction); b.setLeft(a.right()); }
            else
            { a.setHeight(box.height() * node.fraction); b.setTop(a.bottom()); }
            self(self, node.children[0], a); self(self, node.children[1], b);
        };
        visit(visit, *this, area); return result;
    }
    bool remove(QUuid id)
    {
        if (id.isNull()) return false;
        if (cell == id) { *this = {}; return true; }
        for (size_t i = 0; i < children.size(); ++i)
            if (children[i].remove(id))
            {
                if (children[i].empty())
                {
                    auto survivor = std::move(children[1 - i]);
                    *this = std::move(survivor);
                }
                return true;
            }
        return false;
    }
    bool move(QUuid source, QUuid target, PlotPlacement placement, bool wholePage = false)
    {
        if (!valid() || !find(source) || order().size() < 2) return false;
        if (placement != PlotPlacement::Swap && placement != PlotPlacement::Left &&
            placement != PlotPlacement::Right && placement != PlotPlacement::Above &&
            placement != PlotPlacement::Below) return false;
        if ((!wholePage && (source == target || !find(target))) ||
            (wholePage && placement == PlotPlacement::Swap)) return false;
        auto next = *this;
        if (placement == PlotPlacement::Swap)
            std::swap(next.find(source)->cell, next.find(target)->cell);
        else
        {
            next.remove(source);
            auto *destination = wholePage ? &next : next.find(target);
            if (!destination) return false;
            const bool before = placement == PlotPlacement::Left || placement == PlotPlacement::Above;
            const auto axis = placement == PlotPlacement::Left || placement == PlotPlacement::Right
                ? Qt::Horizontal : Qt::Vertical;
            auto old = std::move(*destination);
            *destination = before ? split(axis, leaf(source), std::move(old))
                                  : split(axis, std::move(old), leaf(source));
        }
        if (!next.valid()) return false;
        *this = std::move(next); return true;
    }
    bool append(QUuid id)
    {
        if (id.isNull() || find(id) || order().size() >= 4) return false;
        if (empty()) { *this = leaf(id); return true; }
        if (!valid()) return false;
        // Preserve a custom arrangement; split its largest region. Ties use
        // visual traversal order, so identical layouts always insert alike.
        const auto boxes = regions();
        QUuid largest; double area = -1;
        for (const auto &candidate : order())
        {
            const auto box = boxes[candidate];
            if (box.width() * box.height() > area)
            { largest = candidate; area = box.width() * box.height(); }
        }
        const auto box = boxes[largest];
        *find(largest) = split(box.width() >= box.height() ? Qt::Horizontal : Qt::Vertical,
                              leaf(largest), leaf(id));
        return true;
    }
};
} // namespace aster::ui
#endif
