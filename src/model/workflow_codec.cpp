// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "model/workflow_codec.h"
#include <QJsonArray>
#include <cmath>
#include <limits>

namespace aster {
namespace {
QString idText(const NodeId& id) { return id.toString(QUuid::WithoutBraces); }
bool integer(const QJsonValue& v, double min, double max)
{
    return v.isDouble() && std::isfinite(v.toDouble()) &&
           std::floor(v.toDouble()) == v.toDouble() && v.toDouble() >= min && v.toDouble() <= max;
}
bool exactKeys(const QJsonObject& object, const QStringList& keys)
{
    if (object.size() != keys.size()) return false;
    for (const auto& key : keys) if (!object.contains(key)) return false;
    return true;
}
}
QJsonObject workflowToJson(const WorkflowDefinition& definition)
{
    QJsonArray nodes, layout;
    for (const auto& node : definition.nodes) {
        QJsonValue input(QJsonValue::Null);
        if (node.input) input = QJsonObject{{"parent", idText(node.input->parent)},
            {"output", node.input->output}, {"slot", slotName(node.input->slot)}};
        nodes.push_back(QJsonObject{{"id", idText(node.id)}, {"method", node.method},
            {"method_version", node.methodVersion}, {"parameters", node.parameters}, {"input", input}});
    }
    for (auto it = definition.layout.begin(); it != definition.layout.end(); ++it)
        layout.push_back(QJsonObject{{"id", idText(it.key())}, {"column", it->column}, {"row", it->row}});
    return {{"schema_version", 1}, {"nodes", nodes}, {"layout", layout}};
}
WorkflowReadResult workflowFromJson(const QJsonObject& json)
{
    WorkflowReadResult result;
    const auto reject = [&](QString path, QString message) { result.validation.add(path, message); };
    if (!exactKeys(json, {"schema_version", "nodes", "layout"}) ||
        !integer(json.value("schema_version"), 1, 1) || !json.value("nodes").isArray() || !json.value("layout").isArray()) {
        reject("workflow", "Unsupported schema version or malformed top-level fields");
        return result;
    }
    const auto nodes = json.value("nodes").toArray();
    const auto layout = json.value("layout").toArray();
    if (nodes.size() > WorkflowNodeLimit || layout.size() > WorkflowNodeLimit) {
        reject("workflow", "Node/layout limit exceeded"); return result;
    }
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        const auto path = QStringLiteral("nodes[%1]").arg(i);
        const auto n = nodes[i].toObject();
        if (!nodes[i].isObject() || !exactKeys(n, {"id", "method", "method_version", "parameters", "input"}) ||
            !n.value("id").isString() || QUuid(n.value("id").toString()).isNull() ||
            !n.value("method").isString() || n.value("method").toString().isEmpty() ||
            !integer(n.value("method_version"), 1, std::numeric_limits<int>::max()) || !n.value("parameters").isObject()) {
            reject(path, "Malformed node fields"); continue;
        }
        NodeDefinition node{QUuid(n.value("id").toString()), n.value("method").toString(),
                            n.value("method_version").toInt(), n.value("parameters").toObject(), std::nullopt};
        const auto input = n.value("input");
        if (!input.isNull()) {
            const auto edge = input.toObject();
            const auto slot = slotFromName(edge.value("slot").toString());
            if (!input.isObject() || !exactKeys(edge, {"parent", "output", "slot"}) ||
                !edge.value("parent").isString() || QUuid(edge.value("parent").toString()).isNull() ||
                !edge.value("output").isString() || edge.value("output").toString().isEmpty() || !slot) {
                reject(path, "Malformed input binding"); continue;
            }
            node.input = InputBinding{QUuid(edge.value("parent").toString()), edge.value("output").toString(), *slot};
        }
        result.definition.nodes.push_back(node);
    }
    for (qsizetype i = 0; i < layout.size(); ++i) {
        const auto p = layout[i].toObject();
        const auto id = QUuid(p.value("id").toString());
        if (!layout[i].isObject() || !exactKeys(p, {"id", "column", "row"}) || id.isNull() ||
            result.definition.layout.contains(id) ||
            !integer(p.value("column"), -GridCoordinateLimit, GridCoordinateLimit) ||
            !integer(p.value("row"), -GridCoordinateLimit, GridCoordinateLimit)) {
            reject(QStringLiteral("layout[%1]").arg(i), "Malformed or duplicate layout entry"); continue;
        }
        result.definition.layout.insert(id, {p.value("column").toInt(), p.value("row").toInt()});
    }
    if (!result.validation.ok()) result.definition = {}; // Never expose a partially decoded graph.
    return result;
}
} // namespace aster
