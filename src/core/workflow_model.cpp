// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "core/workflow_model.h"
#include <QHash>
#include <QSet>
#include <algorithm>

namespace aster {
namespace {
QString idText(const NodeId& id) { return id.toString(QUuid::WithoutBraces); }
const NodeDefinition* findNode(const WorkflowDefinition& definition, const NodeId& id)
{
    for (const auto& node : definition.nodes) if (node.id == id) return &node;
    return nullptr;
}
}

ValidationResult validateWorkflow(const WorkflowDefinition& d, const TaskRegistry& registry, bool requireNonEmpty)
{
    ValidationResult result;
    if (requireNonEmpty && d.nodes.isEmpty()) result.add(QStringLiteral("workflow"), QStringLiteral("No source nodes"));
    if (d.nodes.size() > WorkflowNodeLimit) {
        result.add(QStringLiteral("workflow"), QStringLiteral("Node limit exceeded"));
        return result;
    }
    QHash<NodeId, const NodeDefinition*> nodes;
    QMap<QPair<int, int>, NodeId> occupied;
    for (const auto& node : d.nodes) {
        const auto path = idText(node.id);
        if (node.id.isNull() || nodes.contains(node.id)) result.add(path, QStringLiteral("Null or duplicate node ID"));
        nodes.insert(node.id, &node);
        const auto* entry = registry.find(node.method);
        if (!entry) result.add(path, QStringLiteral("Unknown method: ") + node.method);
        else {
            if (node.methodVersion != entry->descriptor.version) result.add(path, QStringLiteral("Method version mismatch"));
            const auto params = registry.normalize(node.method, node.parameters);
            for (const auto& issue : params.validation.issues) result.add(path + QLatin1Char('/') + issue.path, issue.message);
            if (entry->descriptor.inputKind.has_value() != node.input.has_value())
                result.add(path, QStringLiteral("A source must be a root; a transform must have a parent"));
        }
        if (!d.layout.contains(node.id) || !validPosition(d.layout.value(node.id)))
            result.add(path, QStringLiteral("Missing or invalid grid position"));
        else {
            const auto p = d.layout.value(node.id);
            const auto key = qMakePair(p.column, p.row);
            if (occupied.contains(key)) result.add(path, QStringLiteral("Grid cell already occupied"));
            occupied.insert(key, node.id);
        }
    }
    for (auto it = d.layout.begin(); it != d.layout.end(); ++it)
        if (!nodes.contains(it.key())) result.add(idText(it.key()), QStringLiteral("Layout has an unknown node"));

    QHash<NodeId, QVector<NodeId>> children;
    QHash<NodeId, QSet<int>> usedSlots;
    QVector<NodeId> roots;
    for (const auto& node : d.nodes) {
        const auto path = idText(node.id);
        if (!node.input) { roots.push_back(node.id); continue; }
        const auto& input = *node.input;
        const auto* parent = nodes.value(input.parent, nullptr);
        if (!parent || input.parent == node.id) {
            result.add(path, QStringLiteral("Missing parent or self-link"));
            continue;
        }
        children[input.parent].push_back(node.id);
        const auto slot = static_cast<int>(input.slot);
        if (slotName(input.slot).isEmpty() || usedSlots[input.parent].contains(slot))
            result.add(path, QStringLiteral("Invalid or already-used child slot"));
        usedSlots[input.parent].insert(slot);
        if (d.layout.contains(input.parent) && d.layout.contains(node.id)) {
            const auto expected = adjacentPosition(d.layout.value(input.parent), input.slot);
            if (!expected || !(*expected == d.layout.value(node.id)))
                result.add(path, QStringLiteral("Child is not in the declared adjacent cell"));
        }
        const auto* parentEntry = registry.find(parent->method);
        const auto* childEntry = registry.find(node.method);
        if (parentEntry && childEntry) {
            const auto& outputs = parentEntry->descriptor.outputs;
            const auto port = std::find_if(outputs.begin(), outputs.end(), [&](const OutputSpec& item) { return item.key == input.output; });
            if (port == outputs.end()) result.add(path, QStringLiteral("Unknown parent output: ") + input.output);
            else if (!childEntry->descriptor.inputKind || port->kind != *childEntry->descriptor.inputKind)
                result.add(path, QStringLiteral("Input/output kind mismatch"));
        }
    }
    // Iterative traversal: deep imported trees cannot overflow the C++ stack.
    QSet<NodeId> visited;
    for (qsizetype i = 0; i < roots.size(); ++i) {
        const auto id = roots[i];
        if (visited.contains(id)) continue;
        visited.insert(id);
        for (const auto& child : children.value(id)) roots.push_back(child);
    }
    if (visited.size() != nodes.size()) result.add(QStringLiteral("workflow"), QStringLiteral("Cycle or unreachable component"));
    return result;
}

EditResult WorkflowModel::commit(WorkflowDefinition candidate, NodeId affected, qsizetype count)
{
    const auto validation = validateWorkflow(candidate, m_registry);
    if (!validation.ok()) return {validation.message(), {}, 0};
    for (auto& node : candidate.nodes)
        node.parameters = m_registry.normalize(node.method, node.parameters).parameters;
    if (m_retainHistory) {
        if (m_undo.size() == HistoryLimit) m_undo.removeFirst();
        m_undo.push_back(m_definition);
        m_redo.clear();
    }
    m_definition = std::move(candidate);
    return {{}, affected, count};
}
EditResult WorkflowModel::addRoot(const QString& method, GridPosition position, QJsonObject params)
{
    const auto* entry = m_registry.find(method);
    if (!entry) return {QStringLiteral("Unknown method"), {}, 0};
    NodeDefinition node{QUuid::createUuid(), method, entry->descriptor.version, std::move(params), std::nullopt};
    auto candidate = m_definition;
    candidate.nodes.push_back(node);
    candidate.layout.insert(node.id, position);
    return commit(std::move(candidate), node.id, 1);
}
EditResult WorkflowModel::addChild(const NodeId& parentId, ChildSlot slot, const QString& method,
                                  QJsonObject params, QString output)
{
    const auto preview = previewChild(parentId, slot, method, params, output);
    if (!preview.ok()) return {preview.error, {}, 0};
    const auto* entry = m_registry.find(method);
    NodeDefinition node{QUuid::createUuid(), method, entry->descriptor.version, std::move(params),
                        InputBinding{parentId, preview.output, slot}};
    auto candidate = m_definition;
    candidate.nodes.push_back(node);
    candidate.layout.insert(node.id, preview.position);
    return commit(std::move(candidate), node.id, 1);
}
ChildPreview WorkflowModel::previewChild(const NodeId& parentId, ChildSlot slot, const QString& method,
                                        QJsonObject params, QString output) const
{
    const auto* parent = findNode(m_definition, parentId);
    const auto* entry = m_registry.find(method);
    if (!parent || !entry) return {QStringLiteral("Unknown parent or method"), {}, {}};
    if (!entry->descriptor.inputKind) return {QStringLiteral("Source tasks must be roots"), {}, {}};
    const auto position = adjacentPosition(m_definition.layout.value(parentId), slot);
    if (!position) return {QStringLiteral("Invalid slot or grid boundary"), {}, {}};
    if (output.isEmpty()) {
        const auto& parentDescriptor = m_registry.find(parent->method)->descriptor;
        QStringList compatible;
        for (const auto& port : parentDescriptor.outputs)
            if (port.kind == *entry->descriptor.inputKind) compatible.push_back(port.key);
        if (compatible.contains(parentDescriptor.defaultOutput)) output = parentDescriptor.defaultOutput;
        else if (compatible.size() == 1) output = compatible[0];
        else if (compatible.isEmpty()) return {QStringLiteral("No compatible parent output"), *position, {}};
        else return {QStringLiteral("Choose one of the compatible parent outputs"), *position, {}};
    }
    NodeDefinition node{QUuid::createUuid(), method, entry->descriptor.version, std::move(params),
                        InputBinding{parentId, output, slot}};
    auto candidate = m_definition;
    candidate.nodes.push_back(node);
    candidate.layout.insert(node.id, *position);
    const auto validation = validateWorkflow(candidate, m_registry);
    return {validation.message(), *position, output};
}
EditResult WorkflowModel::setParameters(const NodeId& id, const QJsonObject& params)
{
    auto candidate = m_definition;
    for (auto& node : candidate.nodes) if (node.id == id) {
        node.parameters = params; // Replacement, not patch; missing fields take defaults.
        return commit(std::move(candidate), id, 1);
    }
    return {QStringLiteral("Unknown node"), {}, 0};
}
QVector<NodeId> WorkflowModel::removalPreview(const NodeId& root) const
{
    if (!findNode(m_definition, root)) return {};
    QHash<NodeId, QVector<NodeId>> children;
    for (const auto& node : m_definition.nodes)
        if (node.input) children[node.input->parent].push_back(node.id);
    QVector<NodeId> removed{root};
    for (qsizetype i = 0; i < removed.size(); ++i)
        for (const auto& child : children.value(removed[i])) removed.push_back(child);
    return removed;
}
EditResult WorkflowModel::removeSubtree(const NodeId& root)
{
    const auto removed = removalPreview(root);
    if (removed.isEmpty()) return {QStringLiteral("Unknown node"), {}, 0};
    QSet<NodeId> ids(removed.begin(), removed.end());
    auto candidate = m_definition;
    candidate.nodes.erase(std::remove_if(candidate.nodes.begin(), candidate.nodes.end(),
        [&](const NodeDefinition& node) { return ids.contains(node.id); }), candidate.nodes.end());
    for (const auto& id : removed) candidate.layout.remove(id);
    return commit(std::move(candidate), root, removed.size());
}
EditResult WorkflowModel::replaceDefinition(WorkflowDefinition definition)
{
    const auto count = definition.nodes.size();
    return commit(std::move(definition), {}, count);
}
EditResult WorkflowModel::undo()
{
    if (m_undo.isEmpty()) return {QStringLiteral("Nothing to undo"), {}, 0};
    m_redo.push_back(m_definition);
    m_definition = m_undo.takeLast();
    return {{}, {}, m_definition.nodes.size()};
}
EditResult WorkflowModel::redo()
{
    if (m_redo.isEmpty()) return {QStringLiteral("Nothing to redo"), {}, 0};
    m_undo.push_back(m_definition);
    m_definition = m_redo.takeLast();
    return {{}, {}, m_definition.nodes.size()};
}
SnapshotResult WorkflowModel::freeze() const
{
    SnapshotResult result;
    result.validation = validateWorkflow(m_definition, m_registry, true);
    if (result.validation.ok()) result.snapshot.reset(new RunSnapshot(m_definition, m_registry));
    return result;
}
} // namespace aster
