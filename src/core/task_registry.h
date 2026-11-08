// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_CORE_TASK_REGISTRY_H
#define ASTER_CORE_TASK_REGISTRY_H

#include "core/task.h"
#include <limits>

namespace aster {
enum class ParameterKind { Number, Integer, Boolean, Text };
struct ParameterSpec {
    QString key, label;
    ParameterKind kind = ParameterKind::Number;
    QJsonValue defaultValue;
    double minimum = -std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::max();
    QStringList choices;
    // Optional presentation metadata. Numeric defaults/ranges remain authoritative
    // above; hiding a field never changes its stored value or normalization.
    QString group{}, help{}, icon{};
    QJsonObject visibleWhen{}; // all named parameters must equal these values
    QString fileFilter{}; // optional file chooser for Text parameters; empty = ordinary text
    bool hideWhenDefault = false; // Compatibility fields stay visible only when configured.
};
struct OutputSpec {
    QString key, label;
    DataKind kind = DataKind::Series;
    bool required = true;
};
struct TaskDescriptor {
    QString key, label;
    QStringList category; // Hierarchical task menu, e.g. {"Preprocess", "Trend"}.
    int version = 1;
    std::optional<DataKind> inputKind; // nullopt -> source task.
    bool requiresSourceBinding = false;
    // Required in-memory source type, or nullopt for deferred sources such as files.
    std::optional<DataKind> boundDataKind;
    QVector<OutputSpec> outputs;
    QString defaultOutput;
    QVector<ParameterSpec> parameters;
    // Binding-only sources are created by their picker, not the generic task menu.
    bool catalogVisible = true;
    // Paired batch execution. Both inputs must have inputKind.
    // The ordinary graph keeps one parent; the batch plan explicitly resolves
    // dataset, output port and optional origin node for A and B.
    bool pairedInput = false;
};

using TaskFactory = std::function<std::unique_ptr<Task>(const QJsonObject&)>;
struct TaskRegistration { TaskDescriptor descriptor; TaskFactory factory; };
struct ParameterResult {
    QJsonObject parameters;
    ValidationResult validation;
};

// Value type. Build the registry once, then share it as const. No singleton,
// static parameter state, UI factory or global mutable registration list.
class TaskRegistry {
public:
    ValidationResult add(TaskDescriptor descriptor, TaskFactory factory);
    const TaskRegistration* find(const QString& key) const;
    QVector<TaskDescriptor> descriptors() const;
    ParameterResult normalize(const QString& key, const QJsonObject& supplied) const;
    std::unique_ptr<Task> create(const QString& key, const QJsonObject& supplied,
                                 QString* error = nullptr) const;
    QString validateOutput(const TaskDescriptor& descriptor, const NodeResult& result) const;
private:
    QMap<QString, TaskRegistration> m_entries;
};
} // namespace aster
#endif
