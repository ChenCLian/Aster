// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/workflow/workflow_layout.h"
#include <QHash>
#include <QSet>
#include <algorithm>

namespace aster::ui
{
QMap<NodeId, QRectF> verticalWorkflowLayout(const WorkflowDefinition &definition)
{
    constexpr double lane = 246, row = 180;
    QHash<NodeId, QVector<const NodeDefinition *>> children;
    QVector<NodeId> roots;
    for (const auto &node : definition.nodes)
        if (node.input)
            children[node.input->parent].push_back(&node);
        else
            roots.push_back(node.id);
    const auto order = [](ChildSlot slot)
    {
        return slot == ChildSlot::Left ? 0 : slot == ChildSlot::Down ? 1 : 2;
    };
    for (auto it = children.begin(); it != children.end(); ++it)
        std::stable_sort(it->begin(), it->end(), [&](auto a, auto b)
                         { return order(a->input->slot) < order(b->input->slot); });
    // Iterative passes also support deeply nested workflows without stack growth.
    QVector<NodeId> traversal = roots;
    QSet<NodeId> seen(roots.begin(), roots.end());
    for (qsizetype i = 0; i < traversal.size(); ++i)
        for (const auto *child : children.value(traversal[i]))
            if (!seen.contains(child->id))
            {
                seen.insert(child->id);
                traversal.push_back(child->id);
            }
    QHash<NodeId, double> widths;
    for (auto it = traversal.crbegin(); it != traversal.crend(); ++it)
    {
        double width = 0;
        for (const auto *child : children.value(*it))
            width += widths.value(child->id);
        widths[*it] = std::max(1., width);
    }
    QHash<NodeId, double> left, depth;
    double cursor = 0;
    for (const auto &root : roots)
    {
        left[root] = cursor;
        cursor += widths.value(root) + 1;
    }
    QMap<NodeId, QRectF> result;
    for (const auto &id : traversal)
    {
        result[id] = {(left.value(id) + (widths.value(id) - 1) / 2) * lane, depth.value(id) * row,
                      WorkflowCardWidth, WorkflowCardHeight};
        double childLeft = left.value(id);
        for (const auto *child : children.value(id))
        {
            left[child->id] = childLeft;
            depth[child->id] = depth.value(id) + 1;
            childLeft += widths.value(child->id);
        }
    }
    return result;
}
} // namespace aster::ui
