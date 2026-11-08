// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PLOT_DATA_LOADER_H
#define ASTER_PLOT_DATA_LOADER_H
#include "io/database/plot_request.h"
#include "io/database/output_reference.h"
namespace aster::ui
{
NodeResult loadCrossSpectrumOutput(const OutputReference &reference);
NodeResult loadPlotData(const QVector<OutputReference> &references, const PlotRequest &request,
                        const CancellationToken &cancel);
}
#endif
