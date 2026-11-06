// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/services/plot_data_loader.h"
#include "io/database/result_repository.h"
#include <QJsonArray>
#include <algorithm>
namespace aster::ui
{
NodeResult loadPlotData(const QVector<OutputReference> &refs, const PlotRequest &request,
                        const CancellationToken &cancel)
{
    if (refs.isEmpty())
        return NodeResult::failure("No plot reference");
    NodeResult result;
    for (int i = 0; i < refs.size(); ++i)
    {
        if (cancel.isCancelled())
            return NodeResult::cancelled();
        ResultRepository repo;
        auto error = repo.open(refs[i].databasePath, false);
        if (!error.isEmpty())
            return NodeResult::failure(error);
        auto layerRequest = request;
        layerRequest.includeFamily = (i == 0);
        auto out = repo.loadPlotPreview(refs[i], layerRequest, cancel);
        if (out.state != NodeState::Succeeded)
            return out;
        if (i == 0)
            result = std::move(out);
        else
        {
            const auto other = out.outputs.value(refs[i].port);
            if (!compatibleCurveAxes(result.outputs.value(refs[0].port), other))
                return NodeResult::failure(
                    "Overlay requires the same data kind and compatible X/Y units");
            const auto key = QString("overlay_%1").arg(i);
            result.outputs[key] = other;
            auto byOutput = result.report["output_bounds"].toObject();
            byOutput[key] = out.report["output_bounds"].toObject().value(refs[i].port);
            result.report["output_bounds"] = byOutput;
            result.report[key] = QString("ID %1 · %2").arg(refs[i].recordId).arg(refs[i].port);
            if (out.report["display_reduced"].toBool())
                result.report["display_reduced"] = true;
            auto bounds = result.report["bounds"].toArray();
            const auto otherBounds = out.report["bounds"].toArray();
            if (bounds.size() == 4 && otherBounds.size() == 4)
            {
                bounds[0] = std::min(bounds[0].toDouble(), otherBounds[0].toDouble());
                bounds[1] = std::max(bounds[1].toDouble(), otherBounds[1].toDouble());
                bounds[2] = std::min(bounds[2].toDouble(), otherBounds[2].toDouble());
                bounds[3] = std::max(bounds[3].toDouble(), otherBounds[3].toDouble());
                result.report["bounds"] = bounds;
            }
            result.report["bytes_read"] = result.report["bytes_read"].toDouble() +
                                           out.report["bytes_read"].toDouble();
        }
    }
    return result;
}
} // namespace aster::ui
