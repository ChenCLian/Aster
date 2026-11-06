// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_TASK_HELPERS_H
#define ASTER_PROCESSING_TASK_HELPERS_H
#include "core/task_registry.h"
namespace aster::task_detail {
inline ParameterSpec number(QString key, QString label, double value, double minimum, double maximum,
                     ParameterKind kind = ParameterKind::Number)
{
    return {std::move(key), std::move(label), kind, value, minimum, maximum, {}};
}
inline TaskDescriptor seriesTransform(QString label, QStringList category)
{
    TaskDescriptor d;
    d.label = std::move(label);
    d.category = std::move(category);
    d.inputKind = DataKind::Series;
    d.outputs = {{"series", "Series", DataKind::Series, true}};
    d.defaultOutput = "series";
    return d;
}
inline bool cancelled(const ExecutionContext& context, qsizetype index)
{
    return index % 1024 == 0 && context.cancellation.isCancelled();
}
}
#endif
