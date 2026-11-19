// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "model/workflow_types.h"
#include <QStringList>
#include <QSet>

namespace aster {
QJsonObject effectiveParameters(const NodeDefinition &node, const QUuid &item)
{
    auto result = node.parameters;
    const auto changes = node.parameterOverrides.constFind(item);
    if (changes != node.parameterOverrides.cend())
        for (auto it = changes->begin(); it != changes->end(); ++it) result[it.key()] = it.value();
    return result;
}
NodeDefinition resolvedNode(NodeDefinition node, const QUuid &item)
{
    node.parameters = effectiveParameters(node,item);
    node.parameterOverrides.clear();
    return node;
}

bool validPosition(GridPosition p)
{
    return p.column >= -GridCoordinateLimit && p.column <= GridCoordinateLimit &&
           p.row >= -GridCoordinateLimit && p.row <= GridCoordinateLimit;
}
std::optional<GridPosition> adjacentPosition(GridPosition p, ChildSlot slot)
{
    if (!validPosition(p)) return {};
    switch (slot) {
    case ChildSlot::Left: --p.column; break;
    case ChildSlot::Right: ++p.column; break;
    case ChildSlot::Down: ++p.row; break;
    default: return {};
    }
    return validPosition(p) ? std::optional<GridPosition>(p) : std::nullopt;
}
GridPosition freeGridPosition(const WorkflowDefinition& definition, GridPosition near)
{
    QSet<int> occupied;
    for (const auto &p : definition.layout)
        if (p.row == near.row) occupied.insert(p.column);
    // One row has more cells than the maximum number of nodes in a workflow.
    while (occupied.contains(near.column))
        near.column = near.column == GridCoordinateLimit ? -GridCoordinateLimit : near.column + 1;
    return near;
}
QString slotName(ChildSlot slot)
{
    switch (slot) {
    case ChildSlot::Left: return QStringLiteral("left");
    case ChildSlot::Right: return QStringLiteral("right");
    case ChildSlot::Down: return QStringLiteral("down");
    case ChildSlot::Append: return QStringLiteral("append");
    }
    return {};
}
std::optional<ChildSlot> slotFromName(const QString& name)
{
    if (name == QStringLiteral("append")) return ChildSlot::Append;
    if (name == QStringLiteral("left")) return ChildSlot::Left;
    if (name == QStringLiteral("right")) return ChildSlot::Right;
    if (name == QStringLiteral("down")) return ChildSlot::Down;
    return {};
}
QString ValidationResult::message() const
{
    QStringList lines;
    for (const auto& issue : issues) lines.push_back(issue.path + QStringLiteral(": ") + issue.message);
    return lines.join(QLatin1Char('\n'));
}
} // namespace aster
