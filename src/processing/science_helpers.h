// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_SCIENCE_HELPERS_H
#define ASTER_PROCESSING_SCIENCE_HELPERS_H
#include "core/task_registry.h"
#include <cmath>
#include <algorithm>

namespace aster::science_detail {
void initializeGsl();
QJsonObject numericBackendReport();
inline ParameterSpec text(QString key, QString label, QString value, QStringList choices = {})
{ ParameterSpec p; p.key = key; p.label = label; p.kind = ParameterKind::Text; p.defaultValue = value; p.choices = choices; return p; }
inline ParameterSpec boolean(QString key, QString label, bool value)
{ ParameterSpec p; p.key = key; p.label = label; p.kind = ParameterKind::Boolean; p.defaultValue = value; return p; }
inline ErrorInfo orderedSeries(const SeriesData& data)
{
    if (data.x.size() < 2) return makeError(ErrorCode::TooFewSamples, {{"actual", double(data.x.size())}, {"minimum", 2}});
    for (qsizetype i = 1; i < data.x.size(); ++i)
        if (!(data.x[i] > data.x[i-1]))
            return makeError(data.x[i] == data.x[i-1] ? ErrorCode::XDuplicate : ErrorCode::XDescending,
                {{"point_before", double(i)}, {"point", double(i + 1)},
                 {"x_before", data.x[i-1]}, {"x", data.x[i]}});
    return {};
}
inline ErrorInfo uniformSpacing(const SeriesData& data, double& spacing)
{
    const auto error = orderedSeries(data); if (!error.isEmpty()) return error;
    spacing = (data.x.back() - data.x.front()) / (data.x.size() - 1);
    if (!std::isfinite(spacing) || spacing <= 0) return makeError(ErrorCode::InvalidSpacing, {{"spacing", spacing}});
    // Uniformity is relative to the sample interval, not the absolute origin.
    // Large timestamps must not turn percent-level jitter into accepted input.
    // If rounding has already destroyed this precision, report the actual
    // stored intervals instead of assuming unobserved, equally spaced samples.
    const double tolerance = spacing * 1e-6;
    for (qsizetype i = 1; i < data.x.size(); ++i)
        if (std::abs((data.x[i] - data.x[i-1]) - spacing) > tolerance)
            return makeError(ErrorCode::XNonUniform,
                {{"point_before", double(i)}, {"point", double(i + 1)},
                 {"actual_spacing", data.x[i] - data.x[i-1]}, {"expected_spacing", spacing},
                 {"tolerance", tolerance}});
    return {};
}
inline SpectrumData spectrum(const QVector<double>& x, QVector<double> y, const QString& unit)
{ return {x, std::move(y), {"Frequency", unit.isEmpty() ? QString() : "1/" + unit}, {"Power", "PSD"}}; }
} // namespace aster::science_detail
#endif
