// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "core/workflow_rewrite.h"
#include <QHash>
#include <QSet>

namespace aster {
namespace {
const NodeDefinition* find(const WorkflowDefinition& graph, NodeId id)
{
    for (const auto& n : graph.nodes) if (n.id == id) return &n;
    return nullptr;
}
bool contains(const QVector<OutputSpec>& ports, const QString& key)
{
    for (const auto& port : ports) if (port.key == key) return true;
    return false;
}
}
QString nodeRewriteAvailability(const WorkflowModel& model, NodeRewriteKind kind, NodeId id)
{
    const auto& graph = model.definition();
    const auto* n = find(graph, id);
    if (!n) return "NODE_EDIT_MISSING";
    if (!n->input) return "NODE_EDIT_SOURCE";
    if (kind == NodeRewriteKind::InsertBefore && graph.nodes.size() >= WorkflowNodeLimit)
        return "NODE_EDIT_LIMIT";
    const auto& registry = model.registry();
    const auto* parent = find(graph, n->input->parent);
    if (registry.find(n->method)->descriptor.pairedInput ||
        (parent && registry.find(parent->method)->descriptor.pairedInput)) return "NODE_EDIT_JOINT";
    if (kind == NodeRewriteKind::Replace)
        for (const auto& child : graph.nodes)
            if (child.input && child.input->parent == id && registry.find(child.method)->descriptor.pairedInput)
                return "NODE_EDIT_JOINT";
    return {};
}
NodeRewriteOptions inspectNodeRewrite(const WorkflowModel& model, NodeRewriteKind kind,
                                     NodeId id, const QString& method)
{
    NodeRewriteOptions result;
    result.error = nodeRewriteAvailability(model, kind, id);
    if (!result.ok()) return result;
    const auto& registry = model.registry();
    const auto* entry = registry.find(method);
    if (!entry || !entry->descriptor.catalogVisible || !entry->descriptor.inputKind ||
        entry->descriptor.pairedInput || entry->descriptor.requiresSourceBinding) {
        result.error = "NODE_EDIT_ORDINARY"; return result;
    }
    const auto* target = find(model.definition(), id);
    if (kind == NodeRewriteKind::Replace && target->method == method) {
        result.error = "NODE_EDIT_SAME"; return result;
    }
    result.parent = target->input->parent;
    result.previousInput = target->input->output;
    const auto* parent = find(model.definition(), result.parent);
    for (const auto& port : registry.find(parent->method)->descriptor.outputs)
        if (port.kind == *entry->descriptor.inputKind) result.inputs.push_back(port);
    if (result.inputs.isEmpty()) { result.error = "NODE_EDIT_INPUT"; return result; }
    for (const auto& child : model.definition().nodes) {
        const bool affected = kind == NodeRewriteKind::InsertBefore ? child.id == id :
            child.input && child.input->parent == id;
        if (!affected) continue;
        NodeOutputMapping mapping{child.id, child.input->output, {}};
        const auto childKind = registry.find(child.method)->descriptor.inputKind;
        for (const auto& port : entry->descriptor.outputs)
            if (childKind && port.kind == *childKind) mapping.choices.push_back(port);
        if (mapping.choices.isEmpty()) { result.error = "NODE_EDIT_OUTPUT"; return result; }
        result.outputs.push_back(std::move(mapping));
    }
    return result;
}
PreparedNodeRewrite prepareNodeRewrite(const WorkflowEditor& editor, const NodeRewriteRequest& request)
{
    PreparedNodeRewrite result;
    if (editor.locked()) { result.error = "NODE_EDIT_LOCKED"; return result; }
    const auto options = inspectNodeRewrite(editor.model(), request.kind, request.target, request.method);
    if (!options.ok()) { result.error = options.error; return result; }
    if (!contains(options.inputs, request.inputOutput) || request.childOutputs.size() != options.outputs.size()) {
        result.error = "NODE_EDIT_MAPPING"; return result;
    }
    for (const auto& mapping : options.outputs)
        if (!contains(mapping.choices, request.childOutputs.value(mapping.child))) {
            result.error = "NODE_EDIT_MAPPING"; return result;
        }
    auto draft = editor.beginEdit();
    auto& graph = draft.state.definition;
    const auto* entry = editor.model().registry().find(request.method);
    qsizetype targetIndex = 0;
    while (graph.nodes[targetIndex].id != request.target) ++targetIndex;
    if (request.kind == NodeRewriteKind::Replace) {
        auto& node = graph.nodes[targetIndex];
        node.method = request.method; node.methodVersion = entry->descriptor.version;
        node.parameters = request.parameters; node.input->output = request.inputOutput;
        for (auto& child : graph.nodes)
            if (child.input && child.input->parent == request.target)
                child.input->output = request.childOutputs.value(child.id);
        result.nodeId = request.target;
    } else {
        const auto original = graph.nodes[targetIndex];
        const auto originalPosition = graph.layout.value(original.id);
        auto binding = *original.input; binding.output = request.inputOutput;
        const auto inserted = QUuid::createUuid();
        graph.nodes.insert(targetIndex, {inserted, request.method, entry->descriptor.version,
                                         request.parameters, binding});
        graph.nodes[targetIndex + 1].input->parent = inserted;
        graph.nodes[targetIndex + 1].input->output = request.childOutputs.value(original.id);
        graph.layout[inserted] = originalPosition;
        const auto subtree = editor.model().removalPreview(original.id);
        QSet<NodeId> moving(subtree.begin(), subtree.end());
        QSet<QPair<int, int>> fixed;
        for (auto i = graph.layout.cbegin(); i != graph.layout.cend(); ++i)
            if (!moving.contains(i.key())) fixed.insert({i->column, i->row});
        bool placed = false;
        QSet<int> tried;
        // Keep the old continuation direction when possible. Alternative slots
        // translate the entire subtree; unrelated branches never move.
        for (auto slot : {original.input->slot, ChildSlot::Down, ChildSlot::Left, ChildSlot::Right}) {
            if (tried.contains(int(slot))) continue;
            tried.insert(int(slot));
            const auto next = adjacentPosition(originalPosition, slot);
            if (!next) continue;
            const int dx = next->column - originalPosition.column, dy = next->row - originalPosition.row;
            bool valid = true;
            for (const auto id : subtree) {
                const auto p = editor.model().definition().layout.value(id);
                const GridPosition shifted{p.column + dx, p.row + dy};
                if (!validPosition(shifted) || fixed.contains({shifted.column, shifted.row})) { valid = false; break; }
            }
            if (!valid) continue;
            for (const auto id : subtree) {
                const auto p = editor.model().definition().layout.value(id);
                graph.layout[id] = {p.column + dx, p.row + dy};
            }
            graph.nodes[targetIndex + 1].input->slot = slot;
            placed = true; break;
        }
        if (!placed) { result.error = "NODE_EDIT_LAYOUT"; return result; }
        result.nodeId = inserted; result.shiftedNodes = subtree.size();
    }
    const auto prepared = editor.prepare(std::move(draft));
    if (!prepared.validation.ok()) result.error = prepared.validation.message();
    else result.edit = prepared.edit;
    return result;
}
} // namespace aster
