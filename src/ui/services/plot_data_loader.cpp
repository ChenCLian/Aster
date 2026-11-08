// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/services/plot_data_loader.h"
#include "ui/results/plot_recipe.h"
#include "io/database/result_repository.h"
#include <QJsonArray>
#include <algorithm>
namespace aster::ui
{
namespace
{
NodeResult incompatibleOverlay(const OutputReference &target, const DataRef &targetData,
                               const OutputReference &input, const DataRef &inputData)
{
    const auto units = [](const DataRef &data) -> QPair<QString, QString> {
        if (const auto *d = dataAs<SeriesData>(data)) return {d->xAxis.unit, d->yAxis.unit};
        if (const auto *d = dataAs<SpectrumData>(data)) return {d->frequencyAxis.unit, d->powerAxis.unit};
        if (const auto *d = dataAs<GridData>(data)) return {d->xAxis.unit, d->yAxis.unit};
        return {};
    };
    const auto a = units(targetData), b = units(inputData);
    return NodeResult::failure(ErrorInfo{"PLOT_OVERLAY_AXES",
        {{"target_record", QString::number(target.recordId)}, {"input_record", QString::number(input.recordId)},
         {"target_x", a.first}, {"target_y", a.second}, {"input_x", b.first}, {"input_y", b.second}},
        "Overlay requires the same data kind and compatible X/Y units"});
}
}
NodeResult loadCrossSpectrumOutput(const OutputReference &ref)
{
    ResultRepository repo;
    auto error = repo.open(ref.databasePath, false);
    if (!error.isEmpty()) return NodeResult::failure(error);
    auto metadata = repo.load(ref.recordId, true);
    if (!metadata.ok()) return NodeResult::failure(metadata.error);
    auto output = repo.loadPlot(ref, 128 * 1024 * 1024);
    if (output.state != NodeState::Succeeded) return output;
    output.report = metadata.run.nodes.value(ref.nodeId).report;
    return output;
}
NodeResult loadPlotData(const QVector<OutputReference> &refs, const PlotRequest &request,
                        const CancellationToken &cancel)
{
    if (refs.isEmpty())
        return NodeResult::failure("No plot reference");
    NodeResult result;
    auto target = refs.front();
    for (int i = 0; i < refs.size(); ++i)
    {
        if (cancel.isCancelled())
            return NodeResult::cancelled();
        if (i > 0 && refs[i].databasePath == refs[0].databasePath &&
            refs[i].runId == refs[0].runId && refs[i].nodeId == refs[0].nodeId &&
            result.outputs.contains(refs[i].port))
        {
            if (!compatiblePlotLayer(target, result.outputs.value(refs[0].port), refs[i], result.outputs.value(refs[i].port)))
                return incompatibleOverlay(refs[0], result.outputs.value(refs[0].port), refs[i], result.outputs.value(refs[i].port));
            // Spectrum families already contain their background/thresholds.
            // Explicit selection makes a family member visible without duplicating it.
            auto explicitPorts = result.report["plot_explicit_ports"].toArray();
            explicitPorts.append(refs[i].port);
            result.report["plot_explicit_ports"] = explicitPorts;
            continue;
        }
        ResultRepository repo;
        auto error = repo.open(refs[i].databasePath, false);
        if (!error.isEmpty())
            return NodeResult::failure(error);
        auto layerRequest = request;
        layerRequest.includeFamily = (i == 0);
        auto out = refs[i].port == "cross_spectrum" ? loadCrossSpectrumOutput(refs[i]) :
            repo.loadPlotPreview(refs[i], layerRequest, cancel);
        if (out.state != NodeState::Succeeded)
            return out;
        if (i == 0)
        {
            target.method = out.report["plot_method"].toString(target.method);
            result = std::move(out);
        }
        else
        {
            const auto other = out.outputs.value(refs[i].port);
            if (!compatiblePlotLayer(target, result.outputs.value(refs[0].port), refs[i], other))
                return incompatibleOverlay(refs[0], result.outputs.value(refs[0].port), refs[i], other);
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
