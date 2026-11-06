// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_RECORD_FILTER_H
#define ASTER_RECORD_FILTER_H
#include "model/workflow_types.h"
#include <QDateTime>
namespace aster
{
struct RecordFilter
{
    QString text;
    QDateTime since, until;
    double minFrequency = -1, maxFrequency = -1;
    // Exact unit, including empty = unknown. Never compare unrelated units.
    QString frequencyUnit;
    bool hasFrequency() const
    {
        return minFrequency >= 0 || maxFrequency >= 0;
    }
    QString validate() const;
};
struct PeakMatch
{
    NodeId node;
    QString port, unit;
    double frequency = 0;
};
struct PeakMatches
{
    QVector<PeakMatch> peaks;
    QString error;
};
} // namespace aster
#endif
