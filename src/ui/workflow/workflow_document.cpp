// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/workflow/workflow_document.h"
#include <QFileInfo>
#include <QSet>
#include "io/files/series_file_reader.h"
#include <algorithm>
namespace aster::ui
{
namespace {
struct TreeIndex {
    QHash<NodeId,const NodeDefinition*> nodes;
    QHash<NodeId,NodeId> roots;
    QVector<NodeId> order;
    explicit TreeIndex(const WorkflowDefinition& graph) {
        QHash<NodeId,QVector<NodeId>> children;
        for (const auto& n:graph.nodes) { nodes[n.id]=&n;
            if(n.input) children[n.input->parent].push_back(n.id);
            else { roots[n.id]=n.id; order.push_back(n.id); }
        }
        for(qsizetype i=0;i<order.size();++i) for(auto child:children.value(order[i])) {
            roots[child]=roots.value(order[i]); order.push_back(child);
        }
    }
};
}
WorkflowDocument::WorkflowDocument(TaskRegistry r, QObject *p) : QObject(p), m_editor(std::move(r))
{
}
const NodeDefinition *WorkflowDocument::node(const NodeId &id) const
{
    for (const auto &n : model().definition().nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}
QString WorkflowDocument::setSources(const NodeId &root, QVector<SourceItem> items)
{
    const auto *n = node(root);
    const auto *entry = n ? model().registry().find(n->method) : nullptr;
    if (!n || n->input || !entry || !entry->descriptor.requiresSourceBinding)
        return "Source bindings require an external-source root";
    QSet<QUuid> ids;
    for (auto &item : items)
    {
        auto id = QUuid(item.provenance["dataset_id"].toString());
        if (id.isNull() || ids.contains(id)) id = QUuid::createUuid();
        ids.insert(id);
        item.provenance["dataset_id"] = id.toString(QUuid::WithoutBraces);
    }
    auto draft = beginEdit();
    QSet<QUuid> removed;
    for (const auto &item : sources().value(root)) {
        const QUuid oldId(item.provenance["dataset_id"].toString());
        if (!ids.contains(oldId)) removed.insert(oldId);
    }
    if (!removed.isEmpty()) {
        const TreeIndex index(draft.state.definition);
        for (auto &target : draft.state.definition.nodes) if (index.roots.value(target.id)==root)
            for (const auto &item : removed) target.parameterOverrides.remove(item);
    }
    draft.state.sources[root] = std::move(items);
    return applyEdit(std::move(draft)).error;
}
void WorkflowDocument::notifyEdit(const WorkflowEditResult &result)
{
    if (!result.ok() || !result.changed()) return;
    // Every observer sees the complete new graph, source bindings and groups.
    if (result.graphChanged || result.groupingChanged) emit changed();
    if (result.sourcesChanged || result.groupingChanged) emit sourcesChanged();
}
WorkflowEditResult WorkflowDocument::commitEdit(const std::shared_ptr<const PreparedWorkflowEdit> &edit)
{
    const auto result = m_editor.commit(edit);
    notifyEdit(result);
    return result;
}
WorkflowEditResult WorkflowDocument::applyEdit(WorkflowEditDraft draft)
{
    const auto result = m_editor.apply(std::move(draft));
    notifyEdit(result);
    return result;
}
EditResult WorkflowDocument::editGraph(const std::function<EditResult(WorkflowModel&)> &operation)
{
    auto draft = beginEdit();
    WorkflowModel candidate(model().registry(), false);
    const auto loaded = candidate.replaceDefinition(draft.state.definition);
    if (!loaded.ok()) return loaded;
    const auto edited = operation(candidate);
    if (!edited.ok()) return edited;
    draft.state.definition = candidate.definition();
    QSet<NodeId> active;
    for (const auto &n : candidate.definition().nodes) active.insert(n.id);
    // Only removed nodes lose bindings. Changing the kind of a retained node
    // must validate its bindings, not silently discard or retarget them.
    for (auto i = draft.state.sources.begin(); i != draft.state.sources.end(); )
        if (!active.contains(i.key())) i = draft.state.sources.erase(i); else ++i;
    for (auto i = draft.state.grouping.begin(); i != draft.state.grouping.end(); )
        if (!active.contains(i.key())) i = draft.state.grouping.erase(i); else ++i;
    const auto committed = applyEdit(std::move(draft));
    return committed.ok() ? edited : EditResult{committed.error, {}, 0};
}
EditResult WorkflowDocument::addRoot(const QString &method, QJsonObject p)
{
    return editGraph([&](WorkflowModel &candidate) {
        int column = 0;
        for (const auto &position : candidate.definition().layout)
            column = std::max(column, position.column + 4);
        return candidate.addRoot(method, {column, 0}, p);
    });
}
EditResult WorkflowDocument::addChild(const NodeId &p, ChildSlot s, const QString &m, QJsonObject q,
                                      QString output)
{
    return editGraph([&](WorkflowModel &candidate) { return candidate.addChild(p, s, m, q, output); });
}
EditResult WorkflowDocument::setParameters(const NodeId &id, const QJsonObject &p)
{
    return editGraph([&](WorkflowModel &candidate) { return candidate.setParameters(id, p); });
}
// The caller may pass node()->id. Publishing the edit replaces that node's
// storage, so keep the identifier by value until the result has been returned.
EditResult WorkflowDocument::setScopedParameters(NodeId id, QUuid item, const QJsonObject &parameters, bool all)
{
    const auto *original = node(id);
    if (!original) return {"Unknown node",{},0};
    const auto normalized = model().registry().normalize(original->method,parameters);
    if (!normalized.validation.ok()) return {normalized.validation.message(),{},0};
    if (!all) {
        bool found = false;
        const auto joint = pairNodeFor(id);
        if (!joint.isNull()) {
            for (const auto &pair : grouping().value(joint)) found |= pair.id==item;
        } else for (const auto &dataset : datasets(id)) found |= dataset.id==item;
        if (!found) return {"Select a dataset or pair belonging to this node",{},0};
    }
    auto draft = beginEdit();
    for (auto &target : draft.state.definition.nodes) if (target.id==id) {
        if (all) {
            QMap<QUuid,QJsonObject> preserved;
            for (const auto &spec : model().registry().find(target.method)->descriptor.parameters)
                if (spec.perDatasetOnly)
                    for (auto it=target.parameterOverrides.cbegin();it!=target.parameterOverrides.cend();++it) {
                        const auto value=effectiveParameters(target,it.key()).value(spec.key);
                        if (value!=spec.defaultValue) preserved[it.key()][spec.key]=value;
                    }
            target.parameters=normalized.parameters;
            for (const auto &spec : model().registry().find(target.method)->descriptor.parameters)
                if (spec.perDatasetOnly) target.parameters[spec.key]=spec.defaultValue;
            target.parameterOverrides=preserved;
        }
        else {
            QJsonObject difference;
            for (auto it=normalized.parameters.begin();it!=normalized.parameters.end();++it)
                if (target.parameters.value(it.key())!=it.value()) difference[it.key()]=it.value();
            if (difference.isEmpty()) target.parameterOverrides.remove(item);
            else target.parameterOverrides[item]=difference;
        }
    }
    const auto result=applyEdit(std::move(draft));
    return {result.error,id,result.changed()?1:0};
}
EditResult WorkflowDocument::moveDatasets(NodeId sourceRoot, NodeId targetRoot,
                                                const QVector<QUuid> &items, bool copySteps)
{
    const auto *source=node(sourceRoot);
    if (!source || source->input || source->method!="file_source" || sourceRoot==targetRoot || items.isEmpty())
        return {"Choose file datasets and a different destination tree",{},0};
    const QSet<QUuid> selected(items.begin(),items.end());
    if (selected.size()!=items.size() || selected.contains(QUuid{})) return {"Invalid dataset selection",{},0};
    auto draft=beginEdit();
    QVector<SourceItem> moved, retained;
    for (const auto &item : draft.state.sources.value(sourceRoot))
        (selected.contains(QUuid(item.provenance["dataset_id"].toString())) ? moved : retained).push_back(item);
    if (moved.size()!=selected.size()) return {"Dataset selection changed",{},0};
    const TreeIndex index(draft.state.definition);
    if (!targetRoot.isNull()) {
        const auto *target=node(targetRoot);
        if (!target || target->input || target->method!="file_source") return {"Destination must be a file tree",{},0};
        for (const auto &existing : draft.state.sources.value(targetRoot))
            for (const auto &item : moved)
                if (existing.provenance["dataset_id"]==item.provenance["dataset_id"] ||
                    existing.provenance["path"]==item.provenance["path"])
                    return {"Destination already contains a selected file",{},0};
    } else {
        QVector<NodeDefinition> copies;
        QMap<NodeId,NodeId> mapping;
        int right=0, left=draft.state.definition.layout.value(sourceRoot).column;
        for (auto it=draft.state.definition.layout.begin();it!=draft.state.definition.layout.end();++it)
            right=std::max(right,it->column);
        for (const auto &n : draft.state.definition.nodes)
            if (n.id==sourceRoot || (copySteps && index.roots.value(n.id)==sourceRoot)) {
                copies.push_back(n); mapping[n.id]=QUuid::createUuid();
                left=std::min(left,draft.state.definition.layout.value(n.id).column);
            }
        targetRoot=mapping.value(sourceRoot);
        for (auto copy : copies) {
            const auto oldId=copy.id; copy.id=mapping.value(oldId);
            if (copy.input) copy.input->parent=mapping.value(copy.input->parent);
            for (auto it=copy.parameterOverrides.begin();it!=copy.parameterOverrides.end();)
                if (!selected.contains(it.key())) it=copy.parameterOverrides.erase(it); else ++it;
            const auto oldPosition=draft.state.definition.layout.value(oldId);
            const GridPosition position{oldPosition.column+right+4-left,oldPosition.row};
            if (!validPosition(position)) return {"No space for the destination tree",{},0};
            draft.state.definition.nodes.push_back(copy);
            draft.state.definition.layout[copy.id]=position;
        }
        // Processing steps are copied; pair bindings remain explicit in the
        // original tree and must be configured separately in the copied tree.
    }
    for (auto &n : draft.state.definition.nodes)
        if (index.roots.value(n.id)==sourceRoot || index.roots.value(n.id)==targetRoot)
            for (const auto &id : selected) n.parameterOverrides.remove(id);
    draft.state.sources[sourceRoot]=retained;
    draft.state.sources[targetRoot]+=moved;
    const auto result=applyEdit(std::move(draft));
    return {result.error,targetRoot,result.ok()?moved.size():0};
}
EditResult WorkflowDocument::removeSubtree(const NodeId &id)
{
    return editGraph([&](WorkflowModel &candidate) { return candidate.removeSubtree(id); });
}
EditResult WorkflowDocument::undo()
{
    const auto result = m_editor.undo(); notifyEdit(result);
    return {result.error, {}, model().definition().nodes.size()};
}
EditResult WorkflowDocument::redo()
{
    const auto result = m_editor.redo(); notifyEdit(result);
    return {result.error, {}, model().definition().nodes.size()};
}
namespace {
QVector<SourceItem> fileSources(const QJsonObject &parameters, const QVector<SourceItem> &old,
                                const QStringList &paths)
{
    QVector<SourceItem> result;
    QSet<QString> seen;
    QHash<QString, SourceItem> previous;
    for (const auto &item : old) previous.insert(item.provenance["path"].toString(), item);
    for (const auto &path : paths)
    {
        const auto full = QFileInfo(path).absoluteFilePath();
        if (seen.contains(full)) continue;
        seen.insert(full);
        if (previous.contains(full)) result.push_back(previous.value(full));
        else
        {
            SeriesFileOptions options;
            applyFileOptions(parameters, options);
            options.autoHeader = true;
            result.push_back({full, {}, {{"path", full}, {"file_options", fileOptionsToJson(options)},
                {"dataset_id", QUuid::createUuid().toString(QUuid::WithoutBraces)}}});
        }
    }
    return result;
}
}
void WorkflowDocument::setFiles(const NodeId &root, const QStringList &paths)
{
    const auto *n = node(root);
    if (!n || n->input || n->method != "file_source") return;
    setSources(root, fileSources(n->parameters, sources().value(root), paths));
}
EditResult WorkflowDocument::importFiles(NodeId root, const QStringList &paths)
{
    if (paths.isEmpty()) return {};
    auto draft = beginEdit();
    const auto *n = node(root);
    QJsonObject parameters;
    if (!n || n->input || n->method != "file_source")
    {
        WorkflowModel candidate(model().registry(), false);
        const auto loaded = candidate.replaceDefinition(draft.state.definition);
        if (!loaded.ok()) return loaded;
        int column = 0;
        for (const auto &position : candidate.definition().layout)
            column = std::max(column, position.column + 4);
        const auto added = candidate.addRoot("file_source", {column, 0});
        if (!added.ok()) return added;
        root = added.nodeId;
        draft.state.definition = candidate.definition();
        parameters = draft.state.definition.nodes.back().parameters;
    }
    else parameters = n->parameters;
    auto combined = files(root); combined.append(paths);
    draft.state.sources[root] = fileSources(parameters, sources().value(root), combined);
    const auto result = applyEdit(std::move(draft));
    return {result.error, result.ok() ? root : NodeId{}, result.ok() ? 1 : 0};
}
QJsonObject WorkflowDocument::fileOptions(const NodeId &root, const QString &path) const
{
    for (const auto &item : sources().value(root))
        if (item.provenance["path"].toString() == path)
            return item.provenance["file_options"].toObject();
    return {};
}
bool WorkflowDocument::setFileOptions(const NodeId &root, const QStringList &paths,
                                      const QJsonObject &options)
{
    const auto *n = node(root);
    if (!n || n->input || n->method != "file_source")
        return false;
    SeriesFileOptions checked;
    if (!applyFileOptions(options, checked).isEmpty())
        return false;
    const QSet<QString> selected(paths.begin(), paths.end());
    const auto normalized = fileOptionsToJson(checked);
    auto draft = beginEdit();
    bool matched = false;
    for (auto &item : draft.state.sources[root])
        if (selected.contains(item.provenance["path"].toString()))
        {
            item.provenance["file_options"] = normalized;
            matched = true;
        }
    return matched && applyEdit(std::move(draft)).ok();
}
QStringList WorkflowDocument::files(const NodeId &root) const
{
    QStringList out;
    for (const auto &item : sources().value(root))
        out.push_back(item.provenance["path"].toString());
    return out;
}
EditResult WorkflowDocument::addDatabaseSource(const OutputReference &ref)
{
    auto draft = beginEdit();
    WorkflowModel candidate(model().registry(), false);
    const auto loaded = candidate.replaceDefinition(draft.state.definition);
    if (!loaded.ok()) return loaded;
    int column = 0;
    for (const auto &position : candidate.definition().layout)
        column = std::max(column, position.column + 4);
    const auto added = candidate.addRoot("database_" + ref.kind.toLower(), {column, 0});
    if (!added.ok()) return added;
    draft.state.definition = candidate.definition();
    draft.state.sources[added.nodeId] = {{QString("ID %1 · %2").arg(ref.recordId).arg(ref.port), {},
        {{"database_source", ref.toJson()}, {"dataset_id", QUuid::createUuid().toString(QUuid::WithoutBraces)}}}};
    const auto result = applyEdit(std::move(draft));
    return result.ok() ? added : EditResult{result.error, {}, 0};
}
NodeId WorkflowDocument::pairNodeFor(NodeId id) const
{
    const auto *n = node(id);
    while (n)
    {
        if (model().registry().find(n->method)->descriptor.pairedInput) return n->id;
        n = n->input ? node(n->input->parent) : nullptr;
    }
    return {};
}
NodeId WorkflowDocument::rootFor(NodeId id) const
{
    auto *n = node(id);
    while (n && n->input) n = node(n->input->parent);
    return n ? n->id : NodeId{};
}
QVector<BatchDataset> WorkflowDocument::datasets(NodeId id) const
{
    const auto root = rootFor(id);
    const auto *n = node(root);
    if (!n) return {};
    if (!model().registry().find(n->method)->descriptor.requiresSourceBinding)
        return {{root, {model().registry().find(n->method)->descriptor.label, {}, {}}}};
    QVector<BatchDataset> result;
    for (const auto &source : sources().value(root))
        result.push_back({QUuid(source.provenance["dataset_id"].toString()), source});
    return result;
}
QString WorkflowDocument::setPairs(NodeId id, QVector<BatchPair> pairs)
{
    const auto *n = node(id);
    if (!n || !n->input || !model().registry().find(n->method)->descriptor.pairedInput)
        return "Select a paired analysis node";
    auto draft = beginEdit();
    QSet<QUuid> retained;
    for (const auto &pair : pairs) retained.insert(pair.id);
    QSet<QUuid> removed;
    for (const auto &pair : grouping().value(id)) if (!retained.contains(pair.id)) removed.insert(pair.id);
    if (!removed.isEmpty())
        for (auto &target : draft.state.definition.nodes) if (pairNodeFor(target.id)==id)
            for (const auto &item : removed) target.parameterOverrides.remove(item);
    draft.state.grouping[id] = std::move(pairs);
    return applyEdit(std::move(draft)).error;
}
BatchPlanResult WorkflowDocument::pairPreview(NodeId id, QUuid pair) const
{
    const auto joint=pairNodeFor(id);
    auto selected=grouping().value(joint);
    if (!pair.isNull()) selected.erase(std::remove_if(selected.begin(),selected.end(),
        [&](const BatchPair& p){return p.id!=pair;}),selected.end());
    if (hasLinkedInputs({{joint,selected}})) {
        BatchPlanResult invalid;
        const TreeIndex index(model().definition());
        QSet<NodeId> included;
        const auto addPath=[&](NodeId next) {
            while(!next.isNull() && !included.contains(next)) {
                const auto* n=index.nodes.value(next);
                if(!n) return false;
                included.insert(next); next=n->input?n->input->parent:NodeId{};
            }
            return true;
        };
        if (!addPath(id) || !index.nodes.contains(joint)) {
            invalid.validation.add("pair","Selected joint node is unavailable"); return invalid;
        }
        QHash<NodeId,QSet<QUuid>> used;
        for(const auto& p:selected) for(const auto& ref:{p.a,p.b}) {
            const auto origin=ref.node.isNull()?index.nodes[joint]->input->parent:ref.node;
            if(!addPath(origin)) { invalid.validation.add("pair","Referenced node no longer exists"); return invalid; }
            used[index.roots.value(origin)].insert(ref.dataset);
        }
        WorkflowDefinition graph;
        BatchSources bindings;
        for(const auto& n:model().definition().nodes) if(included.contains(n.id)) {
            graph.nodes.push_back(n); graph.layout[n.id]=model().definition().layout.value(n.id);
            if(!n.input) {
                auto data=datasets(n.id);
                if(model().registry().find(n.method)->descriptor.requiresSourceBinding)
                    data.erase(std::remove_if(data.begin(),data.end(),[&](const BatchDataset& d){return !used[n.id].contains(d.id);}),data.end());
                bindings[n.id]=data;
            }
        }
        WorkflowModel scoped(model().registry(),false);
        const auto loaded=scoped.replaceDefinition(graph);
        if(!loaded.ok()) {invalid.validation.add("pair",loaded.error);return invalid;}
        auto plan=prepareLinkedBatch(scoped.freeze().snapshot,bindings,{{joint,selected}});
        if(!plan.validation.ok() || pair.isNull()) return plan;
        return batchDependencySlice(plan.snapshot,id,pair);
    }
    WorkflowDefinition path;
    auto *n = node(id);
    while (n)
    {
        path.nodes.prepend(*n);
        path.layout[n->id] = model().definition().layout.value(n->id);
        n = n->input ? node(n->input->parent) : nullptr;
    }
    BatchPlanResult invalid;
    if (path.nodes.isEmpty()) { invalid.validation.add("pair", "Missing node"); return invalid; }
    WorkflowModel scoped(model().registry());
    auto changed = scoped.replaceDefinition(path);
    if (!changed.ok()) { invalid.validation.add("pair", changed.error); return invalid; }
    BatchGrouping groups;
    for (const auto &definition : path.nodes)
        if (model().registry().find(definition.method)->descriptor.pairedInput)
        {
            auto selected = grouping().value(definition.id);
            if (!pair.isNull())
            {
                selected.erase(std::remove_if(selected.begin(), selected.end(),
                    [&](const BatchPair &p) { return p.id != pair; }), selected.end());
            }
            groups[definition.id] = selected;
        }
    auto selectedData = datasets(id);
    // Preview executes only the chosen members, not every unassigned file.
    if (!pair.isNull())
    {
        QSet<QUuid> used;
        for (const auto &list : groups) for (const auto &p : list)
            { used.insert(p.a.dataset); used.insert(p.b.dataset); }
        selectedData.erase(std::remove_if(selectedData.begin(), selectedData.end(),
            [&](const BatchDataset &d) { return !used.contains(d.id); }), selectedData.end());
    }
    return prepareBatch(scoped.freeze().snapshot, path.nodes.front().id, selectedData, groups);
}
RunPlan WorkflowDocument::plan() const
{
    auto frozen = model().freeze();
    if (!frozen.validation.ok()) return {{}, frozen.validation};
    if (hasLinkedInputs(grouping())) {
        const TreeIndex index(model().definition());
        QHash<NodeId,QSet<NodeId>> links;
        QSet<NodeId> jointRoots;
        SourceSelection active;
        for(const auto& n:model().definition().nodes) {
            const auto& d=model().registry().find(n.method)->descriptor;
            if(!n.input && d.requiresSourceBinding) active[n.id]=sources().value(n.id);
            if(d.pairedInput) jointRoots.insert(index.roots[n.id]);
        }
        const auto groups=grouping();
        for(auto it=groups.cbegin();it!=groups.cend();++it) {
            const auto root=index.roots.value(it.key());
            for(const auto& p:it.value()) for(const auto& ref:{p.a,p.b}) if(!ref.node.isNull()) {
                if(!index.nodes.contains(ref.node)) {
                    RunPlan invalid; invalid.validation.add("grouping","Referenced node no longer exists"); return invalid;
                }
                const auto other=index.roots[ref.node]; links[root].insert(other); links[other].insert(root);
            }
        }
        auto result=prepareRuns(frozen.snapshot,active);
        if(!result.validation.ok()) return result;
        QSet<NodeId> visited, batchedRoots;
        for(auto root:index.order) if(!index.nodes[root]->input && !visited.contains(root)) {
            QVector<NodeId> component{root}; visited.insert(root); bool joint=false;
            for(qsizetype i=0;i<component.size();++i) {
                joint|=jointRoots.contains(component[i]);
                for(auto next:links.value(component[i])) if(!visited.contains(next)) {visited.insert(next);component.push_back(next);}
            }
            if(!joint) continue;
            const QSet<NodeId> members(component.begin(),component.end());
            BatchGrouping selected; BatchSources bindings;
            for(auto n:component) { bindings[n]=datasets(n); batchedRoots.insert(n); }
            for(auto id:index.order) if(members.contains(index.roots[id]) && model().registry().find(index.nodes[id]->method)->descriptor.pairedInput)
                selected[id]=groups.value(id);
            auto batch=hasLinkedInputs(selected)?prepareLinkedBatch(frozen.snapshot,bindings,selected):
                prepareBatch(frozen.snapshot,root,bindings.value(root),selected);
            if(!batch.validation.ok()) return {{},batch.validation};
            result.batches.push_back(batch.snapshot);
        }
        result.requests.erase(std::remove_if(result.requests.begin(),result.requests.end(),
            [&](const RunRequest& request){return batchedRoots.contains(request.root);}),result.requests.end());
        return result;
    }
    SourceSelection active;
    QSet<NodeId> jointRoots;
    for (const auto &n : model().definition().nodes)
    {
        const auto &d = model().registry().find(n.method)->descriptor;
        if (!n.input && d.requiresSourceBinding) active.insert(n.id, sources().value(n.id));
        if (d.pairedInput) jointRoots.insert(rootFor(n.id));
    }
    auto result = prepareRuns(frozen.snapshot, active);
    if (!result.validation.ok()) return result;
    result.requests.erase(std::remove_if(result.requests.begin(), result.requests.end(),
        [&](const RunRequest &r) { return jointRoots.contains(r.root); }), result.requests.end());
    for (const auto &root : jointRoots)
    {
        BatchGrouping groups;
        for (const auto &n : model().definition().nodes)
            if (rootFor(n.id) == root && model().registry().find(n.method)->descriptor.pairedInput)
                groups[n.id] = grouping().value(n.id);
        auto batch = prepareBatch(frozen.snapshot, root, datasets(root), groups);
        if (!batch.validation.ok()) return {{}, batch.validation};
        result.batches.push_back(batch.snapshot);
    }
    return result;
}
EditResult WorkflowDocument::reset()
{
    return replaceDefinition({});
}
EditResult WorkflowDocument::replaceDefinition(const WorkflowDefinition &definition)
{
    return editGraph([&](WorkflowModel &candidate) { return candidate.replaceDefinition(definition); });
}
} // namespace aster::ui
