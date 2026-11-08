// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "core/workflow_editor.h"
#include <QHash>
#include <QSet>

namespace aster {
namespace {
bool sameGraph(const WorkflowDefinition& a, const WorkflowDefinition& b)
{
    if (a.nodes.size() != b.nodes.size() || a.layout != b.layout) return false;
    for (qsizetype i = 0; i < a.nodes.size(); ++i) {
        const auto& x = a.nodes[i]; const auto& y = b.nodes[i];
        if (x.id != y.id || x.method != y.method || x.methodVersion != y.methodVersion ||
            x.parameters != y.parameters || x.input.has_value() != y.input.has_value()) return false;
        if (x.input && (x.input->parent != y.input->parent || x.input->output != y.input->output ||
                        x.input->slot != y.input->slot)) return false;
    }
    return true;
}
bool sameSources(const SourceSelection& a, const SourceSelection& b)
{
    if (a.size() != b.size()) return false;
    auto j = b.cbegin();
    for (auto i = a.cbegin(); i != a.cend(); ++i, ++j) {
        if (i.key() != j.key() || i->size() != j->size()) return false;
        for (qsizetype k = 0; k < i->size(); ++k) {
            const auto& x = i.value()[k]; const auto& y = j.value()[k];
            if (x.label != y.label || x.data != y.data || x.provenance != y.provenance) return false;
        }
    }
    return true;
}
bool sameGroups(const BatchGrouping& a, const BatchGrouping& b)
{
    if (a.size() != b.size()) return false;
    auto j = b.cbegin();
    for (auto i = a.cbegin(); i != a.cend(); ++i, ++j) {
        if (i.key() != j.key() || i->size() != j->size()) return false;
        for (qsizetype k = 0; k < i->size(); ++k) {
            const auto& x = i.value()[k]; const auto& y = j.value()[k];
            if (x.id != y.id || x.label != y.label || !(x.a == y.a) || !(x.b == y.b)) return false;
        }
    }
    return true;
}
WorkflowEditResult difference(const WorkflowEditState& a, const WorkflowEditState& b)
{ return {{}, !sameGraph(a.definition, b.definition), !sameSources(a.sources, b.sources),
            !sameGroups(a.grouping, b.grouping)}; }
// Graph validity is already established. Build indexes once, including deep
// trees, instead of walking to the root separately for every pair/dataset.
struct Index {
    QHash<NodeId, const NodeDefinition*> nodes;
    QHash<NodeId, NodeId> roots;
    QHash<NodeId, bool> pairedAbove;
    Index(const WorkflowDefinition& definition, const TaskRegistry& registry) {
        QHash<NodeId, QVector<NodeId>> children;
        QVector<NodeId> order;
        for (const auto& n : definition.nodes) {
            nodes[n.id] = &n;
            if (n.input) children[n.input->parent].push_back(n.id);
            else { roots[n.id] = n.id; order.push_back(n.id); }
        }
        for (qsizetype i = 0; i < order.size(); ++i) {
            const auto id = order[i];
            const bool paired = registry.find(nodes[id]->method)->descriptor.pairedInput;
            for (const auto child : children.value(id)) {
                // Copy before inserting: QHash growth invalidates references
                // returned by operator[] on the same hash.
                roots[child] = roots.value(id); pairedAbove[child] = pairedAbove.value(id) || paired;
                order.push_back(child);
            }
        }
    }
};
QString refKey(const BatchSeriesRef& ref, NodeId parent)
{ return (ref.node.isNull() ? parent : ref.node).toString(QUuid::WithoutBraces) + ':' +
         ref.dataset.toString(QUuid::WithoutBraces) + ':' + ref.port; }
}

WorkflowEditDraft WorkflowEditor::beginEdit() const
{
    WorkflowEditDraft draft;
    draft.state = m_state; draft.owner = m_identity; draft.revision = m_revision;
    return draft;
}
PreparedEditResult WorkflowEditor::prepare(WorkflowEditDraft draft) const
{
    PreparedEditResult out;
    if (m_locked) out.validation.add("edit", "Workflow editing is locked while running");
    else if (draft.owner != m_identity || draft.revision != m_revision)
        out.validation.add("edit", "The workflow changed; prepare the edit again");
    if (!out.validation.ok()) return out;
    out.validation = validateWorkflow(draft.state.definition, m_model.registry());
    if (!out.validation.ok()) return out;
    for (auto& node : draft.state.definition.nodes)
        node.parameters = m_model.registry().normalize(node.method, node.parameters).parameters;
    out.validation = validateBindings(draft.state);
    if (!out.validation.ok()) return out;
    auto prepared = std::make_shared<PreparedWorkflowEdit>();
    prepared->m_state = std::move(draft.state);
    prepared->m_owner = m_identity; prepared->m_revision = m_revision;
    out.edit = std::move(prepared);
    return out;
}
ValidationResult WorkflowEditor::validateBindings(const WorkflowEditState& candidate) const
{
    ValidationResult result;
    const auto& registry = m_model.registry();
    const Index index(candidate.definition, registry), old(m_state.definition, registry);
    QHash<NodeId, QSet<QUuid>> datasetIds, identities;
    QSet<NodeId> jointRoots;
    for (const auto& n : candidate.definition.nodes)
        if (registry.find(n.method)->descriptor.pairedInput) jointRoots.insert(index.roots.value(n.id));
    // Previously validated immutable data need not be scanned on every keystroke.
    QSet<const DataObject*> knownData;
    for (const auto& list : m_state.sources) for (const auto& item : list)
        if (item.data) knownData.insert(item.data.get());
    for (auto it = candidate.sources.cbegin(); it != candidate.sources.cend(); ++it) {
        const auto* node = index.nodes.value(it.key());
        const auto* d = node ? &registry.find(node->method)->descriptor : nullptr;
        if (!d || node->input || !d->requiresSourceBinding) {
            result.add("sources", "Source bindings require an external-source root"); return result;
        }
        if (jointRoots.contains(it.key()) && it->size() > BatchDatasetLimit) {
            result.add("sources", "Dataset limit exceeded"); return result;
        }
        auto& ids = datasetIds[it.key()];
        for (const auto& item : it.value()) {
            const auto id = QUuid(item.provenance["dataset_id"].toString());
            if (id.isNull() || ids.contains(id)) {
                result.add("sources", "Missing or duplicate dataset identity"); return result;
            }
            ids.insert(id);
            if (item.data && !knownData.contains(item.data.get())) {
                const auto error = validateData(item.data);
                if (!error.isEmpty()) { result.add("sources", error); return result; }
                knownData.insert(item.data.get());
            }
            if (d->boundDataKind && (!item.data || dataKind(*item.data) != *d->boundDataKind)) {
                result.add("sources", "Bound source kind mismatch"); return result;
            }
        }
    }
    for (const auto& n : candidate.definition.nodes) {
        const auto& d = registry.find(n.method)->descriptor;
        if (!n.input && !d.requiresSourceBinding) datasetIds[n.id].insert(n.id);
        if (d.pairedInput && index.pairedAbove.value(n.id)) {
            result.add("grouping", "Grouping outputs of a prior pair is not supported in this batch version");
            return result;
        }
    }
    identities = datasetIds;
    QHash<NodeId, qsizetype> pairCounts;
    for (auto it = candidate.grouping.cbegin(); it != candidate.grouping.cend(); ++it) {
        const auto* n = index.nodes.value(it.key());
        if (!n || !n->input || !registry.find(n->method)->descriptor.pairedInput) {
            result.add("grouping", "Grouping must target a paired analysis node"); return result;
        }
        const auto root = index.roots.value(n->id);
        if ((pairCounts[root] += it->size()) > BatchPairLimit) {
            result.add("grouping", "Batch pair limit exceeded"); return result;
        }
        const auto kind = registry.find(n->method)->descriptor.inputKind;
        QHash<QUuid, BatchPair> previous;
        const auto* priorNode = old.nodes.value(n->id);
        // Only existing references in the same source context may stay unresolved
        // after source removal. New references must resolve immediately.
        if (priorNode && priorNode->input && priorNode->method == n->method &&
            priorNode->input->parent == n->input->parent && old.roots.value(n->id) == root)
            for (const auto& p : m_state.grouping.value(n->id)) previous.insert(p.id, p);
        QSet<QPair<QString, QString>> bindings;
        auto& ids = identities[root];
        for (const auto& pair : it.value()) {
            const auto binding = qMakePair(refKey(pair.a,n->input->parent), refKey(pair.b,n->input->parent));
            if (pair.id.isNull() || ids.contains(pair.id) || binding.first == binding.second || bindings.contains(binding)) {
                result.add("grouping", "Missing/repeated group identity, duplicate pair or identical inputs"); return result;
            }
            ids.insert(pair.id); bindings.insert(binding);
            const auto prior = previous.constFind(pair.id);
            const auto valid = [&](const BatchSeriesRef& ref, bool a) {
                if (ref.dataset.isNull() || ref.port.isEmpty()) return false;
                const bool unchanged = prior != previous.cend() && ref == (a ? prior->a : prior->b);
                const auto origin = ref.node.isNull() ? n->input->parent : ref.node;
                const auto* source = index.nodes.value(origin);
                // Keep a deleted origin as an unresolved reference. It can be
                // repaired or restored by undo; never bind by name or position.
                if (!source) return unchanged;
                const auto& producer = registry.find(source->method)->descriptor;
                if (producer.pairedInput || index.pairedAbove.value(origin)) return false;
                bool compatible = false;
                for (const auto& port : producer.outputs)
                    if (port.key == ref.port && port.kind == kind) compatible = true;
                if (!compatible) return false;
                const auto originRoot = index.roots.value(origin);
                if (candidate.sources.value(originRoot).size() > BatchDatasetLimit) return false;
                return datasetIds.value(originRoot).contains(ref.dataset) || unchanged;
            };
            if (!valid(pair.a, true) || !valid(pair.b, false)) {
                result.add("grouping", "Pair member or compatible output is absent from this upstream batch"); return result;
            }
        }
    }
    return result;
}
WorkflowEditResult WorkflowEditor::publish(WorkflowEditState state)
{
    auto changes = difference(m_state, state);
    if (!changes.changed()) return changes;
    // The graph was validated in prepare (or in a previous history state).
    const auto graph = m_model.replaceDefinition(state.definition);
    if (!graph.ok()) return {graph.error};
    m_state = std::move(state); ++m_revision;
    return changes;
}
WorkflowEditResult WorkflowEditor::commit(const std::shared_ptr<const PreparedWorkflowEdit>& edit)
{
    if (m_locked) return {"Workflow editing is locked while running"};
    if (!edit || edit->m_owner != m_identity || edit->m_revision != m_revision)
        return {"The workflow changed; prepare the edit again"};
    const auto before = m_state;
    auto result = publish(edit->m_state);
    if (result.ok() && result.changed()) {
        if (m_undo.size() == HistoryLimit) m_undo.removeFirst();
        m_undo.push_back(before); m_redo.clear();
    }
    return result;
}
WorkflowEditResult WorkflowEditor::apply(WorkflowEditDraft draft)
{
    const auto prepared = prepare(std::move(draft));
    return prepared.validation.ok() ? commit(prepared.edit) : WorkflowEditResult{prepared.validation.message()};
}
WorkflowEditResult WorkflowEditor::undo()
{
    if (m_locked) return {"Workflow editing is locked while running"};
    if (m_undo.isEmpty()) return {"Nothing to undo"};
    const auto before = m_state;
    auto result = publish(m_undo.back());
    if (result.ok()) { m_undo.removeLast(); m_redo.push_back(before); }
    return result;
}
WorkflowEditResult WorkflowEditor::redo()
{
    if (m_locked) return {"Workflow editing is locked while running"};
    if (m_redo.isEmpty()) return {"Nothing to redo"};
    const auto before = m_state;
    auto result = publish(m_redo.back());
    if (result.ok()) { m_redo.removeLast(); m_undo.push_back(before); }
    return result;
}
void WorkflowEditor::setLocked(bool locked)
{
    if (m_locked == locked) return;
    m_locked = locked; ++m_revision; // A popup prepared before a run is stale after it, too.
}
} // namespace aster
