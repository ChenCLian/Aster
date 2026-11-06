// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "core/workflow_executor.h"
#include "model/workflow_codec.h"
#include <QHash>
#include <exception>
#include <new>
#include <algorithm>

namespace aster
{
namespace
{
const NodeDefinition *findNode(const WorkflowDefinition &d, const NodeId &id)
{
    for (const auto &node : d.nodes)
        if (node.id == id)
            return &node;
    return nullptr;
}
ErrorInfo checkedInput(const DataRef &data, DataKind expected)
{
    const QJsonObject context{{"expected", dataKindName(expected)},
                             {"actual_type", data ? dataKindName(dataKind(*data)) : QStringLiteral("null")}};
    if (data && dataKind(*data) != expected)
        return makeError(ErrorCode::InputType, context);
    const auto error = validateData(data);
    return error.isEmpty() ? ErrorInfo{} : makeError(ErrorCode::InputInvalid, context, error);
}
} // namespace

RunPlan prepareRuns(std::shared_ptr<const RunSnapshot> snapshot, const SourceSelection &selection)
{
    RunPlan plan;
    if (!snapshot)
    {
        plan.validation.add("workflow", "Null snapshot");
        return plan;
    }
    const auto &definition = snapshot->definition();
    const auto &registry = snapshot->registry();
    for (auto it = selection.begin(); it != selection.end(); ++it)
    {
        const auto *node = findNode(definition, it.key());
        if (!node || node->input || !registry.find(node->method)->descriptor.requiresSourceBinding)
            plan.validation.add(it.key().toString(),
                                "Selection belongs to a missing, non-source or generated node");
    }
    for (const auto &node : definition.nodes)
    {
        if (node.input)
            continue;
        const auto &descriptor = registry.find(node.method)->descriptor;
        if (descriptor.requiresSourceBinding)
        {
            const auto items = selection.value(node.id);
            if (items.isEmpty())
                plan.validation.add(node.id.toString(), "Select at least one input dataset");
            for (const auto &item : items)
                plan.requests.push_back({snapshot, node.id, item});
        }
        else
        {
            plan.requests.push_back({snapshot, node.id, {descriptor.label, {}, {}}});
        }
    }
    if (!plan.validation.ok())
        plan.requests.clear();
    return plan;
}

QString runStateName(RunState state)
{
    switch (state)
    {
    case RunState::Succeeded:
        return QStringLiteral("Succeeded");
    case RunState::PartialFailed:
        return QStringLiteral("PartialFailed");
    case RunState::Failed:
        return QStringLiteral("Failed");
    case RunState::Cancelled:
        return QStringLiteral("Cancelled");
    case RunState::Invalid:
        return QStringLiteral("Invalid");
    }
    return QStringLiteral("Invalid");
}

RunResult WorkflowExecutor::run(const RunRequest &request, EventSink sink,
                                CancellationToken cancellation) const
{
    RunResult run;
    run.runId = QUuid::createUuid();
    run.root = request.root;
    run.startedAt = QDateTime::currentDateTimeUtc();
    run.sourceLabel = request.source.label;
    run.provenance = request.source.provenance;
    const auto invalid = [&](const QString &message)
    {
        run.error = message;
        run.finishedAt = QDateTime::currentDateTimeUtc();
        return run;
    };
    if (!request.workflow)
        return invalid(QStringLiteral("Null workflow snapshot"));
    const auto &definition = request.workflow->definition();
    const auto &registry = request.workflow->registry();
    const auto *root = findNode(definition, request.root);
    if (!root || root->input)
        return invalid(QStringLiteral("Run root must be a source node"));
    const auto &rootDescriptor = registry.find(root->method)->descriptor;
    if (!rootDescriptor.requiresSourceBinding && request.source.data)
        return invalid(QStringLiteral("Generator does not accept an external data binding"));

    QHash<NodeId, const NodeDefinition *> nodes;
    QHash<NodeId, QVector<NodeId>> children;
    for (const auto &node : definition.nodes)
    {
        nodes.insert(node.id, &node);
        if (node.input)
            children[node.input->parent].push_back(node.id);
    }
    QVector<NodeId> order{request.root};
    WorkflowDefinition scoped;
    for (qsizetype i = 0; i < order.size(); ++i)
    {
        const auto id = order[i];
        scoped.nodes.push_back(*nodes.value(id));
        scoped.layout.insert(id, definition.layout.value(id));
        for (const auto &child : children.value(id))
            order.push_back(child);
    }
    run.workflowSnapshot = workflowToJson(scoped);
    const auto emitEvent =
        [&](const NodeId &id, NodeState state, const QString &message = QString(), const ErrorInfo &diagnostic = ErrorInfo(), int progress = -1)
    {
        if (!sink)
            return;
        try
        {
            sink({run.runId, id, state, message, run.root, run.sourceLabel, diagnostic, progress});
        }
        catch (const std::exception &e)
        {
            run.observerWarnings.push_back(QString::fromUtf8(e.what()));
        }
        catch (...)
        {
            run.observerWarnings.push_back(
                QStringLiteral("Event observer threw an unknown exception"));
        }
    };
    for (const auto &id : order)
        emitEvent(id, NodeState::Pending);

    bool succeeded = false, failed = false, cancelled = false;
    for (const auto &id : order)
    {
        const auto &node = *nodes.value(id);
        const auto &descriptor = registry.find(node.method)->descriptor;
        NodeResult result;
        TaskInput input;
        if (cancellation.isCancelled())
        {
            result = NodeResult::cancelled();
        }
        else if (node.input && run.nodes.value(node.input->parent).state != NodeState::Succeeded)
        {
            result.state = NodeState::Blocked;
            result.error = QStringLiteral("Parent did not succeed");
            const auto &parent = run.nodes[node.input->parent];
            const auto upstream = parent.diagnostic();
            const auto cause = upstream.context.value("cause_node_id").toString();
            result.setDiagnostic(makeError(ErrorCode::UpstreamFailed,
                {{"parent_id", node.input->parent.toString(QUuid::WithoutBraces)},
                 {"cause_node_id", cause.isEmpty() ? node.input->parent.toString(QUuid::WithoutBraces) : cause},
                 {"cause_code", upstream.code == errorCodeName(ErrorCode::UpstreamFailed)
                                    ? upstream.context.value("cause_code").toString() : upstream.code},
                 {"parent_state", nodeStateName(parent.state)}}));
        }
        else
        {
            if (node.input)
                input.data = run.nodes.value(node.input->parent).outputs.value(node.input->output);
            QString error;
            ErrorInfo inputError;
            if (descriptor.inputKind)
            {
                // A successful producer may omit an optional output. Never guess
                // another port or pass a null pointer to the consumer.
                if (node.input && !run.nodes.value(node.input->parent).outputs.contains(node.input->output))
                    inputError = makeError(ErrorCode::InputUnavailable,
                        {{"expected", dataKindName(*descriptor.inputKind)}});
                else
                    inputError = checkedInput(input.data, *descriptor.inputKind);
            }
            else if (descriptor.boundDataKind)
            {
                inputError = checkedInput(request.source.data, *descriptor.boundDataKind);
            }
            if (!inputError.isEmpty())
                result = NodeResult::failure(inputError);
            else
            {
                auto task = registry.create(node.method, node.parameters, &error);
                if (!task)
                    result = NodeResult::failure(makeError(ErrorCode::ParameterInvalid, {}, error));
                else
                {
                    emitEvent(id, NodeState::Running);
                    // A callback may have requested cancellation at the Running event.
                    if (cancellation.isCancelled())
                        result = NodeResult::cancelled();
                    else
                    {
                        run.executionOrder.push_back(id);
                        try
                        {
                            result = task->execute(
                                input,
                                {run.runId, id, cancellation,
                                 descriptor.requiresSourceBinding ? request.source.data : DataRef{},
                                 request.source.provenance, [&](int percent, const QString& phase) {
                                     if (!cancellation.isCancelled())
                                         emitEvent(id, NodeState::Running, phase, {}, std::clamp(percent, 0, 100));
                                 }});
                            if (result.state == NodeState::Succeeded)
                            {
                                error = registry.validateOutput(descriptor, result);
                                if (!error.isEmpty())
                                    result = NodeResult::failure(makeError(ErrorCode::OutputInvalid, {}, error));
                            }
                            else if (result.state != NodeState::Failed &&
                                     result.state != NodeState::Cancelled)
                            {
                                result = NodeResult::failure(makeError(ErrorCode::TaskContract, {}, QStringLiteral(
                                    "Task returned a non-terminal or executor-only state")));
                            }
                        }
                        catch (const std::bad_alloc &)
                        {
                            result = NodeResult::failure(makeError(ErrorCode::AllocationFailure));
                        }
                        catch (const std::exception &e)
                        {
                            result = NodeResult::failure(makeError(ErrorCode::TaskException, {}, QString::fromUtf8(e.what())));
                        }
                        catch (...)
                        {
                            result = NodeResult::failure(makeError(ErrorCode::TaskException, {},
                                QStringLiteral("Task threw an unknown exception")));
                        }
                    }
                }
            }
        }
        if (result.state != NodeState::Succeeded)
            result.outputs.clear();
        if (result.state == NodeState::Failed && result.error.isEmpty())
            result.error = QStringLiteral("Task failed");
        if (result.state != NodeState::Succeeded)
        {
            auto diagnostic = result.diagnostic();
            if (diagnostic.isEmpty()) diagnostic = makeError(ErrorCode::Unknown, {}, result.error);
            diagnostic.context.insert("method", node.method);
            diagnostic.context.insert("node_id", id.toString(QUuid::WithoutBraces));
            diagnostic.context.insert("run_id", run.runId.toString(QUuid::WithoutBraces));
            if (node.input)
            {
                diagnostic.context.insert("input_port", node.input->output);
                diagnostic.context.insert("parent_id", node.input->parent.toString(QUuid::WithoutBraces));
            }
            result.setDiagnostic(diagnostic);
        }
        succeeded |= result.state == NodeState::Succeeded;
        failed |= result.state == NodeState::Failed || result.state == NodeState::Blocked;
        cancelled |= result.state == NodeState::Cancelled;
        run.nodes.insert(id, result);
        emitEvent(id, result.state, result.error, result.diagnostic());
    }
    run.state = cancelled ? RunState::Cancelled
                : failed  ? (succeeded ? RunState::PartialFailed : RunState::Failed)
                          : RunState::Succeeded;
    run.finishedAt = QDateTime::currentDateTimeUtc();
    return run;
}
} // namespace aster
