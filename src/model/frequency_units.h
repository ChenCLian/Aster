// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>
#include <cmath>
#include <limits>
namespace aster
{
inline QString periodUnitForFrequency(const QString &frequencyUnit)
{
    if (frequencyUnit == "Hz") return "s";
    for (const auto prefix : {"1/", "cycles/"})
        if (frequencyUnit.startsWith(QLatin1String(prefix)))
            return frequencyUnit.mid(qsizetype(qstrlen(prefix)));
    return {};
}
inline double positiveReciprocal(double value)
{
    if (std::isfinite(value) && value > 0) {
        const double result = 1. / value;
        if (std::isfinite(result) && result > 0) return result;
    }
    return std::numeric_limits<double>::quiet_NaN();
}
}
