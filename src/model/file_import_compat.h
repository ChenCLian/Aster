// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_FILE_IMPORT_COMPAT_H
#define ASTER_FILE_IMPORT_COMPAT_H
#include "model/workflow_types.h"

namespace aster {
// Older workflows stored the option both on file nodes and in per-file settings.
inline QJsonObject currentFileImportOptions(QJsonObject options)
{
    options.remove(QStringLiteral("missing_value_code"));
    return options;
}
inline void upgradeFileImportOverrides(NodeDefinition &node)
{
    if (node.method != QStringLiteral("file_source")) return;
    for (auto it = node.parameterOverrides.begin(); it != node.parameterOverrides.end();) {
        it.value() = currentFileImportOptions(it.value());
        if (it.value().isEmpty()) it = node.parameterOverrides.erase(it);
        else ++it;
    }
}
inline void upgradeFileImportMetadata(const QString &method, QJsonObject &metadata)
{
    if (method != QStringLiteral("file_source")) return;
    const auto stored = metadata.value(QStringLiteral("file_options"));
    if (!stored.isObject()) return;
    const auto before = stored.toObject();
    const auto current = currentFileImportOptions(before);
    if (current != before) metadata.insert(QStringLiteral("file_options"), current);
}
} // namespace aster
#endif
