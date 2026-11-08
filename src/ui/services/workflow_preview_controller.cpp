// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/services/workflow_preview_controller.h"
#include <QFileInfo>
#include "ui/results/preview_data.h"
#include "ui/common/presentation.h"
#include "model/workflow_codec.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QThread>
#include <QSet>
namespace aster::ui
{
RunPlan prepareNodePreview(const WorkflowModel &model, NodeId target, NodeId sourceRoot,
                           const SourceItem &source)
{
    WorkflowDefinition path;
    QMap<NodeId, NodeDefinition> nodes;
    for (const auto &n : model.definition().nodes) nodes.insert(n.id, n);
    QSet<NodeId> seen;
    auto id = target;
    while (!id.isNull() && nodes.contains(id) && !seen.contains(id))
    {
        seen.insert(id);
        const auto n = nodes.value(id);
        path.nodes.prepend(n); path.layout[id] = model.definition().layout.value(id);
        id = n.input ? n.input->parent : NodeId{};
    }
    RunPlan invalid;
    if (path.nodes.isEmpty() || !id.isNull() || path.nodes.front().input)
    { invalid.validation.add("preview", "Missing or invalid ancestor path"); return invalid; }
    const auto root = path.nodes.front();
    const auto *entry = model.registry().find(root.method);
    if (!entry) { invalid.validation.add("preview", "Unknown source method"); return invalid; }
    if (entry->descriptor.requiresSourceBinding && (sourceRoot != root.id || source.label.isEmpty()))
    { invalid.validation.add("preview", "Select an input belonging to this source"); return invalid; }
    WorkflowModel scoped(model.registry());
    const auto edit = scoped.replaceDefinition(std::move(path));
    if (!edit.ok()) { invalid.validation.add("preview", edit.error); return invalid; }
    const auto frozen = scoped.freeze();
    if (!frozen.validation.ok()) return {{}, frozen.validation};
    SourceSelection binding;
    if (entry->descriptor.requiresSourceBinding) binding[root.id] = {source};
    return prepareRuns(frozen.snapshot, binding);
}
WorkflowPreviewController::WorkflowPreviewController(WorkflowDocument *doc, QObject *parent)
    : QObject(parent), m_document(doc)
{
    m_timer.setSingleShot(true); m_timer.setInterval(220);
    connect(&m_timer, &QTimer::timeout, this, [this] { m_due = true; if (!m_worker) start(); });
    connect(doc, &WorkflowDocument::changed, this, &WorkflowPreviewController::refresh);
    connect(doc, &WorkflowDocument::sourcesChanged, this, &WorkflowPreviewController::refresh);
}
WorkflowPreviewController::~WorkflowPreviewController()
{
    m_cancel.cancel();
    if (m_worker) { m_worker->wait(); delete m_worker; }
}
void WorkflowPreviewController::setSource(NodeId root, SourceItem source, bool ready)
{
    m_sourceRoot = root; m_source = std::move(source); m_ready = ready; refresh();
}
void WorkflowPreviewController::selectPair(QUuid pair)
{
    if (m_pair == pair) return;
    m_pair = pair; refresh();
}
void WorkflowPreviewController::selectNode(NodeId node)
{
    if (node.isNull() || !m_document->node(node)) return;
    m_node = node; refresh();
}
void WorkflowPreviewController::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active; refresh();
}
void WorkflowPreviewController::setEditing(bool editing)
{
    if (m_editing == editing) return;
    m_editing = editing; refresh();
}
void WorkflowPreviewController::cancel()
{
    m_active = false; refresh();
}
void WorkflowPreviewController::refresh()
{
    if (!m_document->node(m_node)) m_node = {};
    if (!m_document->pairNodeFor(m_node).isNull())
    { refreshPair(); return; }
    m_batch.reset();
    auto plan = prepareNodePreview(m_document->model(), m_node, m_sourceRoot, m_source);
    const bool generator = plan.validation.ok() && !plan.requests.isEmpty() &&
        !plan.requests.front().workflow->registry().find(plan.requests.front().workflow->definition().nodes.front().method)->descriptor.requiresSourceBinding;
    State next = !m_active ? State::Paused : m_node.isNull() ? State::Idle : m_editing ? State::Waiting
        : (!plan.validation.ok() || (!generator && !m_ready)) ? State::Idle : State::Waiting;
    QByteArray key = QByteArray::number(int(next)) + (m_editing ? "edit" : "clean");
    if (plan.validation.ok() && !plan.requests.isEmpty())
    {
        const auto &request = plan.requests.front();
        key += QJsonDocument(workflowToJson(request.workflow->definition())).toJson(QJsonDocument::Compact);
        key += QJsonDocument(request.source.provenance).toJson(QJsonDocument::Compact);
        key += request.source.label.toUtf8();
        key += QByteArray::number(quintptr(request.source.data.get()));
        const QFileInfo f(request.source.provenance["path"].toString());
        key += QByteArray::number(f.size()) + ':' + QByteArray::number(f.lastModified().toMSecsSinceEpoch());
    }
    if (key == m_key) return; // Unrelated branches, blank clicks, catalog browsing: no rerun.
    m_key = key; ++m_generation; m_cancel.cancel(); m_timer.stop(); m_due = false;
    m_result.reset(); m_request = {}; m_message.clear(); m_state = next;
    if (next == State::Idle && !m_node.isNull()) m_message = text("ui.preview_choose_input");
    if (next == State::Waiting && !m_editing && plan.validation.ok() && !plan.requests.isEmpty())
    { m_request = plan.requests.front(); m_timer.start(); }
    emit changed();
}
void WorkflowPreviewController::refreshPair()
{
    auto pairs = m_document->grouping().value(m_document->pairNodeFor(m_node));
    auto pair = m_pair;
    if (pair.isNull() && !pairs.isEmpty()) pair = pairs.front().id;
    auto plan = m_document->pairPreview(m_node, pair);
    State next = !m_active ? State::Paused : m_editing ? State::Waiting :
        (!plan.validation.ok() || pair.isNull()) ? State::Idle : State::Waiting;
    QByteArray key = "pair:" + QByteArray::number(int(next)) + (m_editing ? "edit" : "clean");
    key += m_node.toByteArray() + pair.toByteArray();
    if (plan.snapshot)
    {
        key += QJsonDocument(plan.snapshot->manifest()).toJson(QJsonDocument::Compact);
        for (const auto &job : plan.snapshot->jobs()) if (job.inputs.isEmpty())
        {
            key += QByteArray::number(quintptr(job.source.data.get()));
            QFileInfo f(job.source.provenance["path"].toString());
            key += QByteArray::number(f.size()) + ':' + QByteArray::number(f.lastModified().toMSecsSinceEpoch());
        }
    }
    else key += plan.validation.message().toUtf8();
    if (key == m_key) return;
    m_key = key; ++m_generation; m_cancel.cancel(); m_timer.stop(); m_due = false;
    m_result.reset(); m_request = {}; m_batch.reset(); m_state = next; m_message.clear();
    if (next == State::Idle) m_message = pairs.isEmpty() ? text("ui.pair_incomplete") : plan.validation.message();
    if (next == State::Waiting && !m_editing && plan.snapshot)
    { m_batch = plan.snapshot; m_timer.start(); }
    emit changed();
}
void WorkflowPreviewController::start()
{
    if ((!m_request.workflow && !m_batch) || !m_active || m_editing) return;
    m_due = false; m_state = State::Running; m_cancel = CancellationToken();
    const auto cancel = m_cancel; const auto generation = m_generation;
    const auto request = m_request; const auto target = m_node; const auto batch = m_batch;
    emit changed(); emit started();
    m_worker = QThread::create([this, request, target, batch, cancel, generation]
    {
        auto result = std::make_shared<WorkflowPreviewResult>();
        try
        {
            if (batch)
            {
                result->paired = true;
                auto joint = batch->jobs().back();
                for (const auto &job : batch->jobs()) if (job.inputs.size() == 2) { joint = job; break; }
                result->pairLabel = joint.label;
                result->labelA = batch->jobs()[joint.inputs[0].producer].label + " · " + joint.inputs[0].port;
                result->labelB = batch->jobs()[joint.inputs[1].producer].label + " · " + joint.inputs[1].port;
                if (batch->linkedInputs()) {
                    const auto label = [&](const BatchDependency& dependency) {
                        const auto& producer = batch->jobs()[dependency.producer];
                        for (const auto& node : batch->workflow().definition().nodes) if (node.id == producer.node) {
                            const auto& descriptor = batch->workflow().registry().find(node.method)->descriptor;
                            return methodLabel(descriptor) + " · " + producer.label + " · " + portLabel(descriptor, dependency.port) +
                                   " [" + node.id.toString(QUuid::WithoutBraces).left(8) + "]";
                        }
                        return producer.label + " · " + dependency.port;
                    };
                    result->labelA = label(joint.inputs[0]); result->labelB = label(joint.inputs[1]);
                }
                const auto compactLabel = [&](const BatchDependency &dependency) {
                    const auto &producer = batch->jobs()[dependency.producer];
                    auto name = producer.label;
                    name.replace('\\', '/');
                    name = QFileInfo(name).fileName();
                    auto output = portLabel(dependency.port);
                    for (const auto &node : batch->workflow().definition().nodes)
                        if (node.id == producer.node)
                            if (const auto *entry = batch->workflow().registry().find(node.method))
                                output = portLabel(entry->descriptor, dependency.port);
                    return name + " · " + output;
                };
                result->displayA = compactLabel(joint.inputs[0]);
                result->displayB = compactLabel(joint.inputs[1]);
                BatchObserver observer;
                observer.completed = [&](const BatchJob &job, const NodeResult &out) {
                    if (out.state == NodeState::Succeeded) result->executed << job.node;
                    for (int i=0;i<2;++i)
                        if (joint.inputs[i].producer == job.index)
                        {
                            auto input = out;
                            input.outputs = {{"series", out.outputs.value(joint.inputs[i].port)}};
                            auto reduced = reducePreviewData(input, cancel);
                            (i == 0 ? result->original : result->second) = reduced;
                        }
                    if (job.node == target && result->failedNode.isNull()) result->output = out;
                    if (out.state == NodeState::Failed && result->failedNode.isNull())
                        { result->failedNode = job.node; result->output = out; }
                };
                const auto execution = BatchWorkflowExecutor().run(batch, {1}, observer, cancel);
                if (!execution.error.isEmpty()) result->output = NodeResult::failure(execution.error);
                result->output = reducePreviewData(result->output, cancel);
            }
            else
            {
            const auto run = WorkflowExecutor().run(request, {}, cancel);
            result->executed = run.executionOrder;
            result->original = reducePreviewData(run.nodes.value(request.root), cancel);
            result->output = run.nodes.contains(target) ? run.nodes.value(target) : NodeResult::failure(run.error);
            for (const auto &id : run.executionOrder)
                if (run.nodes.value(id).state == NodeState::Failed)
                { result->failedNode = id; result->output = run.nodes.value(id); break; }
            result->output = reducePreviewData(result->output, cancel);
            }
        }
        catch (const std::exception &e) { result->output = NodeResult::failure(QString::fromUtf8(e.what())); }
        catch (...) { result->output = NodeResult::failure(makeError(ErrorCode::Unknown)); }
        if (cancel.isCancelled()) return;
        QMetaObject::invokeMethod(this, [this, generation, result]
        {
            if (generation != m_generation) return;
            m_result = result;
            m_state = result->output.state == NodeState::Succeeded ? State::Ready : State::Failed;
            emit changed();
        }, Qt::QueuedConnection);
    });
    connect(m_worker, &QThread::finished, this, [this]
    {
        auto *old = m_worker; m_worker = nullptr; old->deleteLater();
        emit busyChanged(false);
        if (m_due) start();
    });
    emit busyChanged(true); m_worker->start();
}
}
