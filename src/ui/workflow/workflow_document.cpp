// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/workflow/workflow_document.h"
#include <QFileInfo>
#include <QSet>
#include "io/files/series_file_reader.h"
#include <algorithm>
namespace aster::ui
{
WorkflowDocument::WorkflowDocument(TaskRegistry r, QObject *p) : QObject(p), m_model(std::move(r))
{
}
const NodeDefinition *WorkflowDocument::node(const NodeId &id) const
{
    for (const auto &n : m_model.definition().nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}
QString WorkflowDocument::setSources(const NodeId &root, QVector<SourceItem> items)
{
    const auto *n = node(root);
    const auto *entry = n ? m_model.registry().find(n->method) : nullptr;
    if (!n || n->input || !entry || !entry->descriptor.requiresSourceBinding)
        return "Source bindings require an external-source root";
    if (entry->descriptor.boundDataKind)
        for (const auto &item : items)
        {
            const auto error = validateData(item.data);
            if (!error.isEmpty())
                return error;
            if (dataKind(*item.data) != *entry->descriptor.boundDataKind)
                return "Bound source kind mismatch";
        }
    m_sources[root] = std::move(items);
    emit sourcesChanged();
    return {};
}
EditResult WorkflowDocument::addRoot(const QString &method, QJsonObject p)
{
    int column = 0;
    for (const auto &position : m_model.definition().layout)
        column = std::max(column, position.column + 4);
    auto r = m_model.addRoot(method, {column, 0}, p);
    if (r.ok())
        emit changed();
    return r;
}
EditResult WorkflowDocument::addChild(const NodeId &p, ChildSlot s, const QString &m, QJsonObject q,
                                      QString output)
{
    auto r = m_model.addChild(p, s, m, q, output);
    if (r.ok())
        emit changed();
    return r;
}
EditResult WorkflowDocument::setParameters(const NodeId &id, const QJsonObject &p)
{
    auto r = m_model.setParameters(id, p);
    if (r.ok())
        emit changed();
    return r;
}
EditResult WorkflowDocument::removeSubtree(const NodeId &id)
{
    auto r = m_model.removeSubtree(id);
    if (r.ok())
        emit changed();
    return r;
}
EditResult WorkflowDocument::undo()
{
    auto r = m_model.undo();
    if (r.ok())
        emit changed();
    return r;
}
EditResult WorkflowDocument::redo()
{
    auto r = m_model.redo();
    if (r.ok())
        emit changed();
    return r;
}
void WorkflowDocument::setFiles(const NodeId &root, const QStringList &paths)
{
    const auto *n = node(root);
    if (!n || n->input || n->method != "file_source")
        return;
    QVector<SourceItem> sources;
    QSet<QString> seen;
    QHash<QString, SourceItem> previous;
    for (const auto &item : m_sources.value(root))
        previous.insert(item.provenance["path"].toString(), item);
    for (const auto &path : paths)
    {
        const auto full = QFileInfo(path).absoluteFilePath();
        if (seen.contains(full))
            continue;
        seen.insert(full);
        if (previous.contains(full))
            sources.push_back(previous.value(full));
        else
        {
            SeriesFileOptions options;
            applyFileOptions(n->parameters, options);
            options.autoHeader = true;
            sources.push_back(
                {full, {}, {{"path", full}, {"file_options", fileOptionsToJson(options)}}});
        }
    }
    setSources(root, std::move(sources));
}
QJsonObject WorkflowDocument::fileOptions(const NodeId &root, const QString &path) const
{
    for (const auto &item : m_sources.value(root))
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
    bool changed = false;
    for (auto &item : m_sources[root])
        if (selected.contains(item.provenance["path"].toString()))
        {
            item.provenance["file_options"] = normalized;
            changed = true;
        }
    if (changed)
        emit sourcesChanged();
    return changed;
}
QStringList WorkflowDocument::files(const NodeId &root) const
{
    QStringList out;
    for (const auto &item : m_sources.value(root))
        out.push_back(item.provenance["path"].toString());
    return out;
}
EditResult WorkflowDocument::addDatabaseSource(const OutputReference &ref)
{
    auto r = addRoot("database_" + ref.kind.toLower());
    if (r.ok())
    {
        setSources(r.nodeId, {{QString("ID %1 · %2").arg(ref.recordId).arg(ref.port),
                                {},
                                {{"database_source", ref.toJson()}}}});
    }
    return r;
}
RunPlan WorkflowDocument::plan() const
{
    auto frozen = m_model.freeze();
    if (!frozen.validation.ok())
        return {{}, frozen.validation};
    SourceSelection active;
    for (const auto &n : m_model.definition().nodes)
        if (!n.input && m_model.registry().find(n.method)->descriptor.requiresSourceBinding)
            active.insert(n.id, m_sources.value(n.id));
    return prepareRuns(frozen.snapshot, active);
}
EditResult WorkflowDocument::reset()
{
    auto r = m_model.replaceDefinition({});
    if (r.ok())
        emit changed();
    return r;
}
EditResult WorkflowDocument::replaceDefinition(const WorkflowDefinition &definition)
{
    auto r = m_model.replaceDefinition(definition);
    if (r.ok())
        emit changed();
    return r;
}
} // namespace aster::ui
