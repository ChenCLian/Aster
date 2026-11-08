// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "core/batch_workflow.h"
#include <QHash>
#include <QJsonArray>
#include <QSet>

namespace aster {
namespace {
QString idText(NodeId id) { return id.toString(QUuid::WithoutBraces); }
struct Index {
    QHash<NodeId, const NodeDefinition*> nodes;
    QHash<NodeId, QVector<NodeId>> children;
    QHash<NodeId, NodeId> roots;
    QSet<NodeId> pairedPath;
    QVector<NodeId> order;
    Index(const RunSnapshot& workflow) {
        for (const auto& n : workflow.definition().nodes) {
            nodes[n.id] = &n;
            if (n.input) children[n.input->parent].push_back(n.id);
            else { order.push_back(n.id); roots[n.id] = n.id; }
        }
        for (qsizetype i=0; i<order.size(); ++i) {
            const auto id = order[i];
            if (workflow.registry().find(nodes[id]->method)->descriptor.pairedInput) pairedPath.insert(id);
            for (auto child : children.value(id)) {
                roots[child] = roots.value(id); if (pairedPath.contains(id)) pairedPath.insert(child);
                order.push_back(child);
            }
        }
    }
};
const OutputSpec* output(const TaskDescriptor& descriptor, const QString& key) {
    for (const auto& port : descriptor.outputs) if (port.key == key) return &port;
    return nullptr;
}
QString refKey(const BatchSeriesRef& ref, NodeId parent) {
    return idText(ref.node.isNull() ? parent : ref.node)+":"+idText(ref.dataset)+":"+ref.port;
}
QJsonObject provenance(const BatchJob& job, const QString& port) {
    return {{"dataset_id",idText(job.item)},{"label",job.label},{"node_id",idText(job.node)},
            {"output",port},{"source_root",idText(job.sourceRoot)},{"source",job.provenance}};
}
}
bool hasLinkedInputs(const BatchGrouping& grouping) {
    for (const auto& pairs : grouping) for (const auto& p : pairs)
        if (!p.a.node.isNull() || !p.b.node.isNull()) return true;
    return false;
}
QVector<NodeId> linkedInputOrigins(const WorkflowModel& model, NodeId joint) {
    const auto frozen = model.freeze();
    if (!frozen.snapshot) return {};
    const Index index(*frozen.snapshot);
    const auto* target = index.nodes.value(joint);
    if (!target || !model.registry().find(target->method)->descriptor.pairedInput) return {};
    const auto kind = model.registry().find(target->method)->descriptor.inputKind;
    QVector<NodeId> result;
    for (auto id : index.order) {
        if (index.pairedPath.contains(id)) continue;
        for (const auto& port : model.registry().find(index.nodes[id]->method)->descriptor.outputs)
            if (port.kind == kind) { result.push_back(id); break; }
    }
    return result;
}
BatchPlanResult prepareLinkedBatch(std::shared_ptr<const RunSnapshot> workflow,
                                  const BatchSources& sources, const BatchGrouping& grouping) {
    BatchPlanResult out;
    const auto reject = [&](const QString& message) { out.validation.add("linked_batch",message); };
    if (!workflow || sources.isEmpty()) { reject("Missing workflow or source scope"); return out; }
    const Index index(*workflow);
    const auto& registry = workflow->registry();
    auto plan=std::shared_ptr<BatchSnapshot>(new BatchSnapshot);
    plan->m_workflow=workflow; plan->m_linked=true; plan->m_grouping=grouping;
    QSet<QUuid> identities;
    for (auto it=sources.cbegin(); it!=sources.cend(); ++it) {
        const auto* root=index.nodes.value(it.key());
        if (!root || root->input) { reject("Source scope must name existing source roots"); return out; }
        const auto& d=registry.find(root->method)->descriptor;
        if (!d.requiresSourceBinding && (it->size()!=1 || it->front().source.data || !it->front().source.provenance.isEmpty())) {
            reject("Generated source must have one unbound dataset identity"); return out;
        }
        if (plan->m_datasets.size()+it->size()>BatchDatasetLimit) { reject("Connected batch dataset limit exceeded"); return out; }
        for (const auto& dataset : it.value()) {
            if (dataset.id.isNull() || identities.contains(dataset.id)) {
                reject("Dataset identities must be unique across the connected source roots"); return out;
            }
            identities.insert(dataset.id); plan->m_datasetIndices[dataset.id]=plan->m_datasets.size();
            plan->m_datasetRoots[dataset.id]=root->id; plan->m_datasets.push_back(dataset);
        }
    }
    QVector<NodeId> ordinary, paired;
    QSet<NodeId> scope;
    for (auto id : index.order) if (sources.contains(index.roots.value(id))) {
        scope.insert(id);
        if (!index.nodes[id]->input) plan->m_roots.push_back(id);
        (index.pairedPath.contains(id) ? paired : ordinary).push_back(id);
    }
    plan->m_root=plan->m_roots.front();
    qsizetype count=0;
    for (auto it=grouping.cbegin(); it!=grouping.cend(); ++it) {
        if (!scope.contains(it.key()) || !registry.find(index.nodes[it.key()]->method)->descriptor.pairedInput) {
            reject("Grouping must target a paired node in the connected source scope"); return out;
        }
        count+=it->size(); if (count>BatchPairLimit) { reject("Connected batch pair limit exceeded"); return out; }
    }
    qsizetype unassignedCount=0;
    QHash<NodeId,QVector<qsizetype>> instances;
    QHash<NodeId,QHash<QUuid,qsizetype>> producers;
    const auto append=[&](BatchJob job) {
        if (plan->m_jobs.size()>=BatchJobLimit) { reject("Batch job limit exceeded"); return false; }
        job.index=plan->m_jobs.size(); job.sourceRoot=index.roots[job.node];
        instances[job.node].push_back(job.index); producers[job.node][job.item]=job.index;
        plan->m_itemJobs[job.item].push_back(job.index); plan->m_jobs.push_back(std::move(job)); return true;
    };
    // Ordinary paths are acyclic trees. All eligible producers are ready in the
    // plan before joint jobs, even if a referenced branch was created later.
    auto order=ordinary; order+=paired;
    for (auto id : order) {
        const auto& node=*index.nodes[id]; const auto& descriptor=registry.find(node.method)->descriptor;
        if (!node.input) {
            for (const auto& d : sources.value(id))
                if (!append({-1,id,d.id,d.source.label,{},d.source,d.source.provenance,{}})) return out;
            continue;
        }
        const auto parent=node.input->parent;
        if (!descriptor.pairedInput) {
            for (auto producer : instances.value(parent)) {
                const auto& previous=plan->m_jobs[producer];
                BatchJob job{-1,id,previous.item,previous.label,{{producer,node.input->output}},{},previous.provenance,{}};
                if (!append(std::move(job))) return out;
            }
            continue;
        }
        if (index.pairedPath.contains(parent)) { reject("Grouping outputs of a prior pair is not supported"); return out; }
        const auto pairs=grouping.value(id);
        if (pairs.isEmpty()) { reject("Configure at least one complete input pair"); return out; }
        QSet<QString> bindings,used;
        QSet<NodeId> origins{parent};
        for (const auto& pair : pairs) {
            const auto aKey=refKey(pair.a,parent),bKey=refKey(pair.b,parent);
            const auto binding=aKey+"/"+bKey;
            if (pair.id.isNull() || identities.contains(pair.id) || aKey==bKey || bindings.contains(binding)) {
                reject("Missing/repeated group identity, duplicate pair or identical inputs"); return out;
            }
            identities.insert(pair.id); bindings.insert(binding);
            QVector<BatchDependency> deps;
            for (const auto& ref : {pair.a,pair.b}) {
                const auto origin=ref.node.isNull()?parent:ref.node;
                if (!scope.contains(origin) || index.pairedPath.contains(origin)) {
                    reject("Joint input must reference an ordinary pre-joint node in the connected scope; self/downstream references are not allowed"); return out;
                }
                const auto* port=output(registry.find(index.nodes[origin]->method)->descriptor,ref.port);
                const auto found=producers.constFind(origin);
                if (!port || port->kind!=descriptor.inputKind || found==producers.cend() || !found->contains(ref.dataset)) {
                    reject("Referenced node, dataset or compatible output is unavailable"); return out;
                }
                origins.insert(origin); used.insert(refKey(ref,parent));
                deps.push_back({found->value(ref.dataset),ref.port});
            }
            const auto& a=plan->m_jobs[deps[0].producer]; const auto& b=plan->m_jobs[deps[1].producer];
            const auto label=pair.label.isEmpty()?a.label+" × "+b.label:pair.label;
            QJsonObject context{{"batch_pair",QJsonObject{{"id",idText(pair.id)},{"label",label},{"node_id",idText(id)},
                {"A",provenance(a,pair.a.port)},{"B",provenance(b,pair.b.port)}}}};
            if (!append({-1,id,pair.id,label,deps,{},context,{}})) return out;
        }
        // Report unused members only for the default parent and explicitly
        // participating origins, never materialize all nodes × all datasets.
        auto& unused=plan->m_unassigned[id];
        for (auto origin : ordinary) if (origins.contains(origin))
            for (auto producer : instances.value(origin))
                for (const auto& port : registry.find(index.nodes[origin]->method)->descriptor.outputs) {
                    BatchSeriesRef ref{plan->m_jobs[producer].item,port.key,origin};
                    if (port.kind==descriptor.inputKind && !used.contains(refKey(ref,parent))) {
                        if (unassignedCount>=BatchJobLimit) { reject("Connected batch unassigned output limit exceeded"); return out; }
                        ++unassignedCount;
                        unused.push_back(ref);
                    }
                }
    }
    if (plan->m_jobs.isEmpty()) { reject("Connected batch has no executable jobs"); return out; }
    out.snapshot=plan; return out;
}
BatchPlanResult batchDependencySlice(std::shared_ptr<const BatchSnapshot> plan, NodeId target, QUuid item) {
    BatchPlanResult result;
    if (!plan) { result.validation.add("preview","Missing batch plan"); return result; }
    QSet<qsizetype> included;
    QVector<qsizetype> order;
    for (const auto& job : plan->m_jobs) if (job.node==target && job.item==item) {
        included.insert(job.index); order.push_back(job.index);
    }
    if (order.isEmpty()) { result.validation.add("preview","Selected node or pair is unavailable"); return result; }
    for (qsizetype i=0;i<order.size();++i) for (const auto& dependency : plan->m_jobs[order[i]].inputs)
        if (!included.contains(dependency.producer)) { included.insert(dependency.producer); order.push_back(dependency.producer); }
    auto sliced=std::shared_ptr<BatchSnapshot>(new BatchSnapshot(*plan));
    sliced->m_jobs.clear(); sliced->m_itemJobs.clear(); sliced->m_grouping.clear(); sliced->m_unassigned.clear();
    QHash<qsizetype,qsizetype> indices;
    for (const auto& original : plan->m_jobs) if (included.contains(original.index)) {
        auto job=original; job.index=sliced->m_jobs.size(); indices[original.index]=job.index;
        for (auto& dep : job.inputs) dep.producer=indices.value(dep.producer,-1);
        sliced->m_itemJobs[job.item].push_back(job.index); sliced->m_jobs.push_back(job);
        if (job.inputs.size()==2) for (const auto& pair : plan->m_grouping.value(job.node))
            if (pair.id==job.item) sliced->m_grouping[job.node].push_back(pair);
    }
    result.snapshot=sliced; return result;
}
} // namespace aster
