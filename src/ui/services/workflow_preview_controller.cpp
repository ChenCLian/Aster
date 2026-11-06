// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/services/workflow_preview_controller.h"
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
void WorkflowPreviewController::start()
{
    if (!m_request.workflow || !m_active || m_editing) return;
    m_due = false; m_state = State::Running; m_cancel = CancellationToken();
    const auto cancel = m_cancel; const auto generation = m_generation;
    const auto request = m_request; const auto target = m_node;
    emit changed(); emit started();
    m_worker = QThread::create([this, request, target, cancel, generation]
    {
        auto result = std::make_shared<WorkflowPreviewResult>();
        try
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
