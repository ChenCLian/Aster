// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "model/workflow_types.h"
#include <QStringList>

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
QString slotName(ChildSlot slot)
{
    switch (slot) {
    case ChildSlot::Left: return QStringLiteral("left");
    case ChildSlot::Right: return QStringLiteral("right");
    case ChildSlot::Down: return QStringLiteral("down");
    }
    return {};
}
std::optional<ChildSlot> slotFromName(const QString& name)
{
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
