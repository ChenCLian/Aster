#include "core/task_registry.h"
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <algorithm>
#include <exception>

namespace aster {
namespace {
bool validKey(const QString& key)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9_]*$"));
    return pattern.match(key).hasMatch();
}
QString parameterError(const ParameterSpec& spec, const QJsonValue& value)
{
    switch (spec.kind) {
    case ParameterKind::Number:
    case ParameterKind::Integer:
        if (!value.isDouble() || !std::isfinite(value.toDouble())) return QStringLiteral("Expected finite number");
        if (spec.kind == ParameterKind::Integer && std::floor(value.toDouble()) != value.toDouble())
            return QStringLiteral("Expected integer");
        if (value.toDouble() < spec.minimum || value.toDouble() > spec.maximum)
            return QStringLiteral("Outside declared range");
        break;
    case ParameterKind::Boolean:
        if (!value.isBool()) return QStringLiteral("Expected boolean");
        break;
    case ParameterKind::Text:
        if (!value.isString()) return QStringLiteral("Expected string");
        if (!spec.choices.isEmpty() && !spec.choices.contains(value.toString()))
            return QStringLiteral("Not a declared choice");
        break;
    default: return QStringLiteral("Unknown parameter kind");
    }
    return {};
}
bool knownKind(DataKind kind)
{
    switch (kind) {
#define KIND_CASE(name) case DataKind::name: return true;
        ASTER_DATA_KINDS(KIND_CASE)
#undef KIND_CASE
    }
    return false;
}
}

ValidationResult TaskRegistry::add(TaskDescriptor d, TaskFactory factory)
{
    ValidationResult validation;
    const auto reject = [&](const QString& message) { validation.add(d.key, message); };
    if (!validKey(d.key) || m_entries.contains(d.key)) reject(QStringLiteral("Invalid or duplicate method key"));
    if (d.version < 1 || d.label.isEmpty() || d.category.isEmpty() || !factory)
        reject(QStringLiteral("Missing label/category/factory or invalid version"));
    if (d.inputKind && !knownKind(*d.inputKind)) reject(QStringLiteral("Unknown input kind"));
    if (d.inputKind && d.requiresSourceBinding) reject(QStringLiteral("Transform cannot require source binding"));
    if (d.boundDataKind && (!d.requiresSourceBinding || !knownKind(*d.boundDataKind)))
        reject(QStringLiteral("Invalid bound data kind"));
    QSet<QString> outputKeys;
    for (const auto& output : d.outputs) {
        if (!validKey(output.key) || outputKeys.contains(output.key) || !knownKind(output.kind))
            reject(QStringLiteral("Invalid or duplicate output declaration"));
        outputKeys.insert(output.key);
    }
    if (d.outputs.isEmpty() || !outputKeys.contains(d.defaultOutput)) reject(QStringLiteral("Invalid default output"));
    QSet<QString> parameterKeys;
    for (const auto& spec : d.parameters) {
        if (!validKey(spec.key) || parameterKeys.contains(spec.key) ||
            !std::isfinite(spec.minimum) || !std::isfinite(spec.maximum) || spec.minimum > spec.maximum)
            reject(QStringLiteral("Invalid or duplicate parameter declaration"));
        if (!spec.fileFilter.isEmpty() && (spec.kind != ParameterKind::Text || !spec.choices.isEmpty()))
            reject(QStringLiteral("File chooser requires a free-text parameter"));
        parameterKeys.insert(spec.key);
        const auto error = parameterError(spec, spec.defaultValue);
        if (!error.isEmpty()) reject(spec.key + QStringLiteral(" default: ") + error);
    }
    for (const auto& p : d.parameters)
        for (auto it=p.visibleWhen.begin();it!=p.visibleWhen.end();++it) {
            const auto found=std::find_if(d.parameters.begin(),d.parameters.end(),[&](const auto& dep){return dep.key==it.key();});
            if(found==d.parameters.end() || found->key==p.key || !parameterError(*found,it.value()).isEmpty())
                reject(QStringLiteral("Invalid parameter visibility condition: ")+p.key);
        }
    if (validation.ok()) {
        const auto key = d.key;
        m_entries.insert(key, {std::move(d), std::move(factory)});
    }
    return validation;
}

const TaskRegistration* TaskRegistry::find(const QString& key) const
{
    const auto it = m_entries.constFind(key);
    return it == m_entries.cend() ? nullptr : &it.value();
}
QVector<TaskDescriptor> TaskRegistry::descriptors() const
{
    QVector<TaskDescriptor> result;
    for (const auto& entry : m_entries) result.push_back(entry.descriptor);
    return result;
}
ParameterResult TaskRegistry::normalize(const QString& key, const QJsonObject& supplied) const
{
    ParameterResult result;
    const auto* entry = find(key);
    if (!entry) { result.validation.add(key, QStringLiteral("Unknown method")); return result; }
    QSet<QString> allowed;
    for (const auto& spec : entry->descriptor.parameters) {
        allowed.insert(spec.key);
        const auto value = supplied.contains(spec.key) ? supplied.value(spec.key) : spec.defaultValue;
        const auto error = parameterError(spec, value);
        if (!error.isEmpty()) result.validation.add(key + QLatin1Char('.') + spec.key, error);
        else result.parameters.insert(spec.key, value);
    }
    for (auto it = supplied.begin(); it != supplied.end(); ++it)
        if (!allowed.contains(it.key())) result.validation.add(key + QLatin1Char('.') + it.key(), QStringLiteral("Unknown parameter"));
    return result;
}

std::unique_ptr<Task> TaskRegistry::create(const QString& key, const QJsonObject& supplied, QString* error) const
{
    if (error) error->clear();
    const auto normalized = normalize(key, supplied);
    if (!normalized.validation.ok()) {
        if (error) *error = normalized.validation.message();
        return {};
    }
    try {
        auto task = find(key)->factory(normalized.parameters);
        if (!task && error) *error = QStringLiteral("Factory returned null");
        return task;
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
    } catch (...) {
        if (error) *error = QStringLiteral("Factory threw an unknown exception");
    }
    return {};
}

QString TaskRegistry::validateOutput(const TaskDescriptor& d, const NodeResult& result) const
{
    if (result.state != NodeState::Succeeded) return QStringLiteral("Expected succeeded result");
    if (!result.error.isEmpty()) return QStringLiteral("Succeeded result carries an error");
    if (!result.diagnostic().isEmpty()) return QStringLiteral("Succeeded result carries a failure diagnostic");
    QSet<QString> declared;
    for (const auto& spec : d.outputs) {
        declared.insert(spec.key);
        if (!result.outputs.contains(spec.key)) {
            if (spec.required) return QStringLiteral("Missing output: ") + spec.key;
            continue;
        }
        const auto data = result.outputs.value(spec.key);
        auto error = validateData(data);
        if (!error.isEmpty()) return spec.key + QStringLiteral(": ") + error;
        if (dataKind(*data) != spec.kind) return QStringLiteral("Output kind mismatch: ") + spec.key;
    }
    for (auto it = result.outputs.begin(); it != result.outputs.end(); ++it)
        if (!declared.contains(it.key())) return QStringLiteral("Undeclared output: ") + it.key();
    return {};
}
} // namespace aster
