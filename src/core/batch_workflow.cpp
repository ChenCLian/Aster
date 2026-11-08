// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "core/batch_workflow.h"
#include "model/workflow_codec.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <future>
#include <limits>
#include <mutex>
#include <type_traits>
#include <vector>

namespace aster {
namespace {
QString idText(const QUuid& id) { return id.toString(QUuid::WithoutBraces); }
bool exactKeys(const QJsonObject& o, const QStringList& keys)
{
    if (o.size() != keys.size()) return false;
    for (const auto& k : keys) if (!o.contains(k)) return false;
    return true;
}
QJsonObject refJson(const BatchSeriesRef& r, bool linked)
{
    QJsonObject out{{"dataset", idText(r.dataset)}, {"port", r.port}};
    if (linked) out["node"] = r.node.isNull() ? QString() : idText(r.node);
    return out;
}
bool readRef(const QJsonValue& value, BatchSeriesRef& ref, bool linked)
{
    const auto o = value.toObject();
    if (!value.isObject() || !exactKeys(o, linked ? QStringList{"dataset", "port", "node"} : QStringList{"dataset", "port"}) ||
        !o["dataset"].isString() || !o["port"].isString()) return false;
    ref = {QUuid(o["dataset"].toString()), o["port"].toString()};
    if (linked) {
        if (!o["node"].isString()) return false;
        ref.node = QUuid(o["node"].toString());
        if (!o["node"].toString().isEmpty() && ref.node.isNull()) return false;
    }
    return !ref.dataset.isNull() && !ref.port.isEmpty();
}
const OutputSpec* outputSpec(const TaskDescriptor& d, const QString& key)
{
    for (const auto& port : d.outputs) if (port.key == key) return &port;
    return nullptr;
}
QString refKey(const BatchSeriesRef& ref)
{ return idText(ref.node) + ':' + idText(ref.dataset) + ':' + ref.port; }
QString pairKey(const BatchPair& pair)
{ return refKey(pair.a) + '/' + refKey(pair.b); }
QJsonObject memberProvenance(const BatchJob& producer, const QString& port)
{
    return {{"dataset_id", idText(producer.item)}, {"label", producer.label},
            {"node_id", idText(producer.node)}, {"output", port},
            {"source", producer.provenance}};
}
ErrorInfo inputError(const DataRef& data, DataKind expected, const QString& role)
{
    QJsonObject context{{"role", role}, {"expected", dataKindName(expected)},
                        {"actual_type", data ? dataKindName(dataKind(*data)) : "null"}};
    if (!data) return makeError(ErrorCode::InputUnavailable, context);
    if (dataKind(*data) != expected) return makeError(ErrorCode::InputType, context);
    const auto error = validateData(data);
    return error.isEmpty() ? ErrorInfo{} : makeError(ErrorCode::InputInvalid, context, error);
}
quint64 payloadBytes(const DataRef& ref)
{
    return std::visit([](const auto& v) -> quint64 {
        using T = std::decay_t<decltype(v)>;
        quint64 count = 0;
        if constexpr (std::is_same_v<T, SeriesData>) count = quint64(v.x.size()) + quint64(v.y.size());
        else if constexpr (std::is_same_v<T, SpectrumData>) count = quint64(v.frequency.size()) + quint64(v.power.size());
        else if constexpr (std::is_same_v<T, GridData>) count = quint64(v.x.size()) + quint64(v.y.size()) + quint64(v.values.size());
        else {
            for (const auto& row : v.rows) count += quint64(row.size());
            quint64 mask = 0;
            for (const auto& row : v.defined) mask += quint64(row.size());
            return count * sizeof(double) + mask;
        }
        return count * sizeof(double);
    }, *ref);
}
struct ExecutedJob { NodeResult result; bool executed = false; };
ExecutedJob executeJob(const BatchSnapshot& plan, const NodeDefinition& node,
                       const BatchJob& job, TaskInput input,
                       RunId batchId, CancellationToken cancel,
                       const std::function<void(NodeState, int, const QString&)>& event)
{
    const auto& registry = plan.workflow().registry();
    const auto& descriptor = registry.find(node.method)->descriptor;
    ExecutedJob out;
    try {
        if (cancel.isCancelled()) { out.result = NodeResult::cancelled(); return out; }
        if (descriptor.inputKind) {
            auto error = inputError(input.data, *descriptor.inputKind, descriptor.pairedInput ? "A" : "input");
            if (error.isEmpty() && descriptor.pairedInput)
                error = inputError(input.second, *descriptor.inputKind, "B");
            if (!error.isEmpty()) { out.result = NodeResult::failure(error); return out; }
        } else if (descriptor.boundDataKind) {
            const auto error = inputError(job.source.data, *descriptor.boundDataKind, "source");
            if (!error.isEmpty()) { out.result = NodeResult::failure(error); return out; }
        }
        QString error;
        auto task = registry.create(node.method, node.parameters, &error);
        if (!task) { out.result = NodeResult::failure(makeError(ErrorCode::ParameterInvalid, {}, error)); return out; }
        event(NodeState::Running, -1, {});
        if (cancel.isCancelled()) { out.result = NodeResult::cancelled(); return out; }
        out.executed = true;
        out.result = task->execute(input,
            {batchId, job.node, cancel, descriptor.requiresSourceBinding ? job.source.data : DataRef{},
             job.provenance, [&](int percent, const QString& phase) {
                 if (!cancel.isCancelled()) event(NodeState::Running, std::clamp(percent, 0, 100), phase);
             }});
        if (out.result.state == NodeState::Succeeded) {
            error = registry.validateOutput(descriptor, out.result);
            if (!error.isEmpty()) out.result = NodeResult::failure(makeError(ErrorCode::OutputInvalid, {}, error));
        } else if (out.result.state != NodeState::Failed && out.result.state != NodeState::Cancelled)
            out.result = NodeResult::failure(makeError(ErrorCode::TaskContract));
    } catch (const std::bad_alloc&) {
        out.result = NodeResult::failure(makeError(ErrorCode::AllocationFailure));
    } catch (const std::exception& e) {
        out.result = NodeResult::failure(makeError(ErrorCode::TaskException, {}, QString::fromUtf8(e.what())));
    } catch (...) {
        out.result = NodeResult::failure(makeError(ErrorCode::TaskException, {}, "Task threw an unknown exception"));
    }
    return out;
}
} // namespace

QJsonObject batchGroupingToJson(const BatchGrouping& grouping)
{
    const bool linked = hasLinkedInputs(grouping);
    QJsonArray nodes;
    for (auto it = grouping.cbegin(); it != grouping.cend(); ++it) {
        QJsonArray pairs;
        for (const auto& p : it.value())
            pairs.append(QJsonObject{{"id", idText(p.id)}, {"label", p.label},
                                     {"a", refJson(p.a, linked)}, {"b", refJson(p.b, linked)}});
        nodes.append(QJsonObject{{"node", idText(it.key())}, {"pairs", pairs}});
    }
    return {{"schema_version", linked ? 2 : 1}, {"nodes", nodes}};
}

BatchGroupingReadResult batchGroupingFromJson(const QJsonObject& json)
{
    BatchGroupingReadResult result;
    const auto reject = [&](const QString& message) { result.validation.add("batch_grouping", message); };
    if (!exactKeys(json, {"schema_version", "nodes"}) || !json["schema_version"].isDouble() ||
        (json["schema_version"].toDouble() != 1 && json["schema_version"].toDouble() != 2) || !json["nodes"].isArray() ||
        json["nodes"].toArray().size() > WorkflowNodeLimit) {
        reject("Unsupported or malformed batch grouping"); return result;
    }
    const bool linked = json["schema_version"].toDouble() == 2;
    QSet<QUuid> pairIds;
    qsizetype total = 0;
    for (const auto& n : json["nodes"].toArray()) {
        const auto o = n.toObject();
        const QUuid node(o["node"].toString());
        if (!n.isObject() || !exactKeys(o, {"node", "pairs"}) || node.isNull() ||
            !o["node"].isString() || !o["pairs"].isArray() || result.grouping.contains(node)) {
            reject("Malformed or duplicate grouping node"); break;
        }
        const auto pairs = o["pairs"].toArray();
        total += pairs.size();
        if (total > BatchPairLimit) { reject("Batch pair limit exceeded"); break; }
        QVector<BatchPair> decoded;
        QSet<QString> bindings;
        for (const auto& v : pairs) {
            const auto p = v.toObject();
            BatchPair pair;
            pair.id = QUuid(p["id"].toString()); pair.label = p["label"].toString();
            if (!v.isObject() || !exactKeys(p, {"id", "label", "a", "b"}) ||
                !p["id"].isString() || pair.id.isNull() || pairIds.contains(pair.id) ||
                !p["label"].isString() || !readRef(p["a"], pair.a, linked) || !readRef(p["b"], pair.b, linked) ||
                pair.a == pair.b || bindings.contains(pairKey(pair))) {
                reject("Malformed, repeated or self-paired input"); break;
            }
            pairIds.insert(pair.id); bindings.insert(pairKey(pair)); decoded.push_back(pair);
        }
        if (!result.validation.ok()) break;
        result.grouping.insert(node, decoded);
    }
    if (!result.validation.ok()) result.grouping.clear();
    return result;
}

BatchPlanResult prepareBatch(std::shared_ptr<const RunSnapshot> workflow, NodeId root,
                             const QVector<BatchDataset>& datasets, const BatchGrouping& grouping)
{
    if (hasLinkedInputs(grouping)) return prepareLinkedBatch(workflow, {{root, datasets}}, grouping);
    BatchPlanResult out;
    const auto reject = [&](QString path, QString message) { out.validation.add(path, message); };
    if (!workflow || datasets.isEmpty() || datasets.size() > BatchDatasetLimit) {
        reject("batch", "Missing workflow/datasets or dataset limit exceeded"); return out;
    }
    auto plan = std::shared_ptr<BatchSnapshot>(new BatchSnapshot);
    plan->m_workflow = workflow; plan->m_root = root;
    plan->m_datasets = datasets; plan->m_grouping = grouping;
    const auto& definition = workflow->definition();
    const auto& registry = workflow->registry();
    QMap<NodeId, NodeDefinition> nodes;
    QMap<NodeId, QVector<NodeId>> children;
    for (const auto& n : definition.nodes) {
        nodes.insert(n.id, n);
        if (n.input) children[n.input->parent].push_back(n.id);
    }
    if (!nodes.contains(root) || nodes[root].input) { reject("batch", "Choose one source root"); return out; }
    const auto& rootDescriptor = registry.find(nodes[root].method)->descriptor;
    if (!rootDescriptor.requiresSourceBinding &&
        (datasets.size() != 1 || datasets[0].source.data || !datasets[0].source.provenance.isEmpty())) {
        reject("batch", "Generated source runs once and cannot receive external source bindings"); return out;
    }
    QSet<QUuid> identities;
    for (const auto& d : datasets) {
        plan->m_datasetIndices[d.id] = plan->m_datasetIndices.size();
        if (d.id.isNull() || identities.contains(d.id)) reject("batch", "Missing or duplicate dataset identity");
        identities.insert(d.id);
    }
    if (!out.validation.ok()) return out;
    QVector<NodeId> order{root};
    for (qsizetype i = 0; i < order.size(); ++i)
        for (const auto& child : children.value(order[i])) order.push_back(child);
    QSet<NodeId> scope(order.begin(), order.end());
    qsizetype pairCount = 0;
    for (auto it = grouping.cbegin(); it != grouping.cend(); ++it) {
        pairCount += it.value().size();
        if (!scope.contains(it.key()) || !registry.find(nodes[it.key()].method)->descriptor.pairedInput)
            reject(idText(it.key()), "Grouping must target a paired node in this source subtree");
    }
    if (pairCount > BatchPairLimit) reject("batch", "Batch pair limit exceeded");
    if (!out.validation.ok()) return out;
    QMap<NodeId, QVector<qsizetype>> instances;
    QMap<NodeId, bool> pairedAncestor;
    qsizetype unassignedCount = 0;
    const auto append = [&](BatchJob job) {
        if (plan->m_jobs.size() >= BatchJobLimit) { reject("batch", "Batch job limit exceeded"); return; }
        job.index = plan->m_jobs.size();
        plan->m_itemJobs[job.item].push_back(job.index);
        instances[job.node].push_back(job.index); plan->m_jobs.push_back(std::move(job));
    };
    for (const auto& id : order) {
        const auto& node = nodes[id];
        const auto& descriptor = registry.find(node.method)->descriptor;
        if (!node.input) {
            for (const auto& d : datasets) {
                append({-1, id, d.id, d.source.label, {}, d.source, d.source.provenance});
                if (!out.validation.ok()) return out;
            }
            continue;
        }
        const auto parent = node.input->parent;
        const auto& producerDescriptor = registry.find(nodes[parent].method)->descriptor;
        pairedAncestor[id] = pairedAncestor.value(parent) || descriptor.pairedInput;
        if (!descriptor.pairedInput) {
            for (const auto producer : instances.value(parent)) {
                const auto previous = plan->m_jobs[producer];
                append({-1, id, previous.item, previous.label, {{producer, node.input->output}}, {}, previous.provenance});
                if (!out.validation.ok()) return out;
            }
            continue;
        }
        if (pairedAncestor.value(parent)) {
            reject(idText(id), "Grouping outputs of a prior pair is not supported in this batch version"); return out;
        }
        if (grouping.value(id).isEmpty()) { reject(idText(id), "Configure at least one complete input pair"); return out; }
        QMap<QUuid, qsizetype> upstream;
        for (const auto index : instances.value(parent)) upstream.insert(plan->m_jobs[index].item, index);
        QSet<QString> used, bindings;
        for (const auto& pair : grouping.value(id)) {
            if (pair.id.isNull() || identities.contains(pair.id) || pair.a == pair.b || bindings.contains(pairKey(pair))) {
                reject(idText(id), "Missing/repeated group identity, duplicate pair or identical inputs"); return out;
            }
            identities.insert(pair.id); bindings.insert(pairKey(pair));
            for (const auto& ref : {pair.a, pair.b}) {
                const auto* port = outputSpec(producerDescriptor, ref.port);
                if (!upstream.contains(ref.dataset) || !port || port->kind != *descriptor.inputKind) {
                    reject(idText(id), "Pair member or compatible output is absent from this upstream batch"); return out;
                }
                used.insert(refKey(ref));
            }
            const auto a = upstream.value(pair.a.dataset), b = upstream.value(pair.b.dataset);
            const auto label = pair.label.isEmpty() ? plan->m_jobs[a].label + " × " + plan->m_jobs[b].label : pair.label;
            QJsonObject provenance{{"batch_pair", QJsonObject{
                {"id", idText(pair.id)}, {"label", label}, {"node_id", idText(id)},
                {"A", memberProvenance(plan->m_jobs[a], pair.a.port)},
                {"B", memberProvenance(plan->m_jobs[b], pair.b.port)}}}};
            append({-1, id, pair.id, label, {{a, pair.a.port}, {b, pair.b.port}}, {}, provenance});
            if (!out.validation.ok()) return out;
        }
        auto& unassigned = plan->m_unassigned[id];
        for (const auto index : instances.value(parent))
            for (const auto& port : producerDescriptor.outputs) {
                const BatchSeriesRef ref{plan->m_jobs[index].item, port.key};
                if (port.kind == *descriptor.inputKind && !used.contains(refKey(ref))) {
                    if (unassignedCount >= BatchJobLimit) { reject(idText(id), "Unassigned output limit exceeded"); return out; }
                    ++unassignedCount;
                    unassigned.push_back(ref);
                }
            }
    }
    out.snapshot = plan;
    return out;
}

QJsonObject BatchSnapshot::manifest(QUuid item) const
{
    QVector<qsizetype> selected;
    QSet<qsizetype> seen;
    if (item.isNull()) {
        selected.reserve(m_jobs.size());
        for (qsizetype i=0;i<m_jobs.size();++i) selected.push_back(i);
    } else {
        selected = m_itemJobs.value(item);
        if (selected.isEmpty()) return {};
        seen = QSet<qsizetype>(selected.begin(),selected.end());
        for (qsizetype i=0;i<selected.size();++i)
            for (const auto& input : m_jobs[selected[i]].inputs)
                if (!seen.contains(input.producer)) {
                    seen.insert(input.producer);selected.push_back(input.producer);
                }
        std::sort(selected.begin(),selected.end());
    }
    QSet<NodeId> included;
    QSet<qsizetype> sourceIndices;
    BatchGrouping groups;
    QJsonArray datasets, jobs;
    for (const auto index : selected) {
        const auto& job = m_jobs[index];
        included.insert(job.node);
        if (job.inputs.isEmpty()) sourceIndices.insert(m_datasetIndices.value(job.item));
        QJsonArray inputs;
        for (const auto& dep : job.inputs) {
            const auto& producer = m_jobs[dep.producer];
            inputs.append(QJsonObject{{"node", idText(producer.node)}, {"item", idText(producer.item)}, {"port", dep.port}});
        }
        if (job.inputs.size()==2) {
            const auto& a=job.inputs[0];const auto& b=job.inputs[1];
            groups[job.node].push_back({job.item,job.label,
                {m_jobs[a.producer].item,a.port,m_linked ? m_jobs[a.producer].node : NodeId{}},
                {m_jobs[b.producer].item,b.port,m_linked ? m_jobs[b.producer].node : NodeId{}}});
        }
        jobs.append(QJsonObject{{"node", idText(job.node)}, {"item", idText(job.item)},
                               {"label", job.label}, {"inputs", inputs}, {"provenance", job.provenance}});
    }
    auto orderedSources=sourceIndices.values();std::sort(orderedSources.begin(),orderedSources.end());
    for (const auto index : orderedSources) {
        const auto& d=m_datasets[index];
        QJsonObject source{{"id", idText(d.id)}, {"label", d.source.label}, {"source", d.source.provenance}};
        if (m_linked) source["root"] = idText(m_datasetRoots.value(d.id));
        datasets.append(source);
    }
    // Retain placement ancestors even when a joint task reads both inputs from
    // other branches. These are graph definitions, not extra executed jobs.
    if (m_linked) {
        QHash<NodeId, NodeId> parents;
        for (const auto& node : m_workflow->definition().nodes)
            if (node.input) parents[node.id] = node.input->parent;
        auto closure = included.values();
        for (qsizetype i = 0; i < closure.size(); ++i) {
            const auto parent = parents.value(closure[i]);
            if (!parent.isNull() && !included.contains(parent)) { included.insert(parent); closure.push_back(parent); }
        }
    }
    WorkflowDefinition scoped;
    for (const auto& node : m_workflow->definition().nodes) if (included.contains(node.id)) {
        scoped.nodes.push_back(node); scoped.layout.insert(node.id, m_workflow->definition().layout.value(node.id));
    }
    auto manifestRoot=m_root;
    if (m_linked && !included.contains(manifestRoot))
        for (auto root : m_roots) if (included.contains(root)) { manifestRoot=root; break; }
    QJsonObject result{{"schema_version", m_linked ? 2 : 1}, {"root", idText(manifestRoot)}, {"workflow", workflowToJson(scoped)},
            {"datasets", datasets}, {"grouping", batchGroupingToJson(item.isNull()?m_grouping:groups)}, {"jobs", jobs}};
    if (m_linked) {
        QJsonArray roots;
        for (auto root : m_roots) if (included.contains(root)) roots.append(idText(root));
        result["roots"] = roots;
    }
    if (!item.isNull()) { result["scope"]="item_dependencies";result["item"]=idText(item); }
    return result;
}

BatchExecutionResult BatchWorkflowExecutor::run(std::shared_ptr<const BatchSnapshot> plan,
                                               BatchExecutionOptions options, BatchObserver observer,
                                               CancellationToken cancellation) const
{
    BatchExecutionResult report;
    if (!plan || plan->jobs().isEmpty() || options.concurrency < 1 || options.concurrency > 256 || !options.maxCachedPayloadBytes) {
        report.error = "Invalid batch plan or execution limits"; return report;
    }
    report.batchId = QUuid::createUuid();
    const auto& jobs = plan->jobs();
    const auto count = jobs.size();
    report.jobs.resize(count);
    QVector<NodeResult> retained(count);
    QMap<NodeId, const NodeDefinition*> definitions;
    for (const auto& node : plan->workflow().definition().nodes) definitions.insert(node.id, &node);
    QVector<QVector<qsizetype>> children(count), parents(count);
    QVector<QSet<QString>> neededPorts(count);
    QVector<bool> submitted(count, false);
    QVector<int> outstanding(count), consumers(count);
    std::deque<qsizetype> runnable;
    for (const auto& job : jobs) {
        QSet<qsizetype> unique;
        for (const auto& dep : job.inputs) {
            unique.insert(dep.producer); neededPorts[dep.producer].insert(dep.port);
        }
        for (const auto p : unique) { children[p].push_back(job.index); parents[job.index].push_back(p); ++consumers[p]; }
        outstanding[job.index] = unique.size();
        if (unique.isEmpty()) runnable.push_back(job.index);
    }
    struct CacheEntry { int references = 0; quint64 bytes = 0; };
    QMap<const DataObject*, CacheEntry> cache;
    quint64 cacheBytes = 0;
    const auto uniqueData = [](const NodeResult& result) {
        QMap<const DataObject*, DataRef> data;
        for (const auto& ref : result.outputs) if (ref) data.insert(ref.get(), ref);
        return data;
    };
    const auto release = [&](qsizetype index) {
        for (const auto& ref : uniqueData(retained[index])) {
            auto it = cache.find(ref.get());
            if (--it->references == 0) { cacheBytes -= it->bytes; cache.erase(it); }
        }
        retained[index] = {};
    };
    std::mutex observerMutex;
    const auto event = [&](qsizetype index, NodeState state, int progress = -1, const QString& message = QString()) {
        if (!observer.event) return;
        std::lock_guard<std::mutex> lock(observerMutex);
        try { observer.event({report.batchId, index, jobs[index].node, jobs[index].item, state, progress, message}); }
        catch (const std::exception& e) { report.observerWarnings.push_back(QString::fromUtf8(e.what())); }
        catch (...) { report.observerWarnings.push_back("Batch event observer threw an unknown exception"); }
    };
    for (const auto& job : jobs) event(job.index, NodeState::Pending);
    qsizetype finished = 0;
    const auto promoteDependency = [&](qsizetype dependency) {
        // Before the first pair, each dataset follows a single-parent path.
        // If A is ready, schedule the missing B path before collecting more A
        // inputs. A reference placed last must not need the entire batch cached.
        while (report.jobs[dependency].state == NodeState::Pending && !submitted[dependency]) {
            if (outstanding[dependency] == 0) {
                const auto it = std::find(runnable.begin(), runnable.end(), dependency);
                if (it != runnable.end()) { runnable.erase(it); runnable.push_front(dependency); }
                return;
            }
            auto next = qsizetype(-1);
            for (const auto parent : parents[dependency])
                if (report.jobs[parent].state == NodeState::Pending) { next = parent; break; }
            if (next < 0) return;
            dependency = next;
        }
    };
    const auto finish = [&](qsizetype index, ExecutedJob done) {
        auto& result = done.result;
        for (const auto p : parents[index]) if (--consumers[p] == 0) release(p);
        if (result.state != NodeState::Succeeded) result.outputs.clear();
        if (result.state == NodeState::Failed && result.error.isEmpty()) result.error = "Task failed";
        if (result.state == NodeState::Succeeded && consumers[index]) {
            NodeResult required;
            for (auto it = result.outputs.cbegin(); it != result.outputs.cend(); ++it)
                if (neededPorts[index].contains(it.key())) required.outputs.insert(it.key(), it.value());
            quint64 added = 0;
            for (const auto& ref : uniqueData(required)) if (!cache.contains(ref.get())) added += payloadBytes(ref);
            if (added > options.maxCachedPayloadBytes - cacheBytes) {
                result = NodeResult::failure(ErrorInfo{"BATCH_CACHED_OUTPUT_LIMIT",
                    {{"cached_bytes", double(cacheBytes)}, {"additional_bytes", double(added)},
                     {"maximum_bytes", double(options.maxCachedPayloadBytes)}},
                    "Intermediate output cache budget exceeded"});
            } else {
                for (const auto& ref : uniqueData(required)) {
                    auto& entry = cache[ref.get()];
                    if (entry.references++ == 0) { entry.bytes = payloadBytes(ref); cacheBytes += entry.bytes; }
                }
                report.peakCachedPayloadBytes = std::max(report.peakCachedPayloadBytes, cacheBytes);
                retained[index] = std::move(required);
            }
        }
        if (result.state != NodeState::Succeeded) {
            auto diagnostic = result.diagnostic();
            if (diagnostic.isEmpty()) diagnostic = makeError(ErrorCode::Unknown, {}, result.error);
            diagnostic.context.insert("batch_id", idText(report.batchId));
            diagnostic.context.insert("node_id", idText(jobs[index].node));
            diagnostic.context.insert("item_id", idText(jobs[index].item));
            diagnostic.context.insert("method", definitions.value(jobs[index].node)->method);
            result.setDiagnostic(diagnostic);
        }
        report.jobs[index] = {result.state, done.executed, result.report, result.error, result.warnings};
        if (observer.completed) {
            try { observer.completed(jobs[index], result); }
            catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(observerMutex); report.observerWarnings.push_back(QString::fromUtf8(e.what()));
            } catch (...) {
                std::lock_guard<std::mutex> lock(observerMutex); report.observerWarnings.push_back("Batch completion observer threw an unknown exception");
            }
        }
        if (observer.finished) {
            try { observer.finished(jobs[index], result, done.executed); }
            catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(observerMutex); report.observerWarnings.push_back(QString::fromUtf8(e.what()));
            } catch (...) {
                std::lock_guard<std::mutex> lock(observerMutex); report.observerWarnings.push_back("Batch persistence observer threw an unknown exception");
            }
        }
        event(index, result.state, -1, result.error);
        ++finished;
        // Prefer consumers of completed data to reduce the retained cache.
        for (auto it = children[index].crbegin(); it != children[index].crend(); ++it)
            if (--outstanding[*it] == 0) runnable.push_front(*it);
        for (const auto child : children[index])
            if (jobs[child].inputs.size() == 2 && outstanding[child] > 0)
                for (const auto parent : parents[child]) promoteDependency(parent);
    };
    std::mutex readyMutex;
    std::condition_variable readyChanged;
    std::vector<qsizetype> ready;
    ready.reserve(static_cast<size_t>(count)); // completion notification must not allocate in a worker
    size_t readyRead = 0;
    // Allocate slots before launching workers. Assignment to a future is
    // noexcept; no failed map insertion can leave an untracked live worker.
    std::vector<std::future<ExecutedJob>> active(static_cast<size_t>(count));
    size_t activeCount = 0;
    while (finished < count) {
        while (!runnable.empty() && activeCount < size_t(options.concurrency)) {
            const auto index = runnable.front(); runnable.pop_front();
            submitted[index] = true;
            if (cancellation.isCancelled()) { finish(index, {NodeResult::cancelled(), false}); continue; }
            qsizetype failedParent = -1;
            for (const auto p : parents[index]) if (report.jobs[p].state != NodeState::Succeeded) { failedParent = p; break; }
            if (failedParent >= 0) {
                auto blocked = NodeResult::failure(makeError(ErrorCode::UpstreamFailed,
                    {{"parent_id", idText(jobs[failedParent].node)}, {"parent_item", idText(jobs[failedParent].item)},
                     {"parent_state", nodeStateName(report.jobs[failedParent].state)}}));
                blocked.state = NodeState::Blocked; finish(index, {blocked, false}); continue;
            }
            TaskInput input;
            if (!jobs[index].inputs.isEmpty()) {
                const auto& a = jobs[index].inputs[0]; input.data = retained[a.producer].outputs.value(a.port);
                if (jobs[index].inputs.size() == 2) {
                    const auto& b = jobs[index].inputs[1]; input.second = retained[b.producer].outputs.value(b.port);
                }
            }
            try {
                active[size_t(index)] = std::async(std::launch::async, [&, index, input] {
                    ExecutedJob done;
                    try {
                        done = executeJob(*plan, *definitions.value(jobs[index].node), jobs[index], input, report.batchId, cancellation,
                                          [&](NodeState s, int p, const QString& m) { event(index, s, p, m); });
                    } catch (const std::exception& e) {
                        done.result = NodeResult::failure(makeError(ErrorCode::TaskException, {}, QString::fromUtf8(e.what())));
                    } catch (...) { done.result = NodeResult::failure(makeError(ErrorCode::TaskException)); }
                    { std::lock_guard<std::mutex> lock(readyMutex); ready.push_back(index); }
                    readyChanged.notify_one(); return done;
                });
                ++activeCount;
            } catch (const std::exception& e) {
                finish(index, {NodeResult::failure(makeError(ErrorCode::TaskException, {}, QString::fromUtf8(e.what()))), false});
            }
        }
        if (!activeCount) {
            if (finished != count) { report.error = "Batch scheduler has unresolved dependencies"; return report; }
            break;
        }
        qsizetype index;
        { std::unique_lock<std::mutex> lock(readyMutex); readyChanged.wait(lock, [&] { return readyRead < ready.size(); });
          index = ready[readyRead++]; }
        auto done = active[size_t(index)].get(); --activeCount; finish(index, std::move(done));
    }
    bool succeeded = false, failed = false, cancelled = false;
    for (const auto& job : report.jobs) {
        succeeded |= job.state == NodeState::Succeeded;
        failed |= job.state == NodeState::Failed || job.state == NodeState::Blocked;
        cancelled |= job.state == NodeState::Cancelled;
    }
    report.state = cancelled ? RunState::Cancelled : failed ? (succeeded ? RunState::PartialFailed : RunState::Failed) : RunState::Succeeded;
    return report;
}
} // namespace aster
