// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_CROSS_SPECTRUM_TASK_H
#define ASTER_CROSS_SPECTRUM_TASK_H
#include "core/task_registry.h"
namespace aster
{
// Same-batch paired Welch analysis; executed by BatchWorkflowExecutor.
class CrossSpectrumTask final : public Task
{
  public:
    explicit CrossSpectrumTask(QJsonObject parameters) : m_parameters(std::move(parameters)) {}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput &, const ExecutionContext &) override;

  private:
    QJsonObject m_parameters;
};
ValidationResult registerCrossSpectrum(TaskRegistry &registry);
// Adapter contract for future curve views: each returned series is a separate
// contiguous path. Undefined cells and wrapped-phase jumps never get joined.
// Empty result is valid when the requested metric is entirely undefined.
struct CrossCurveResult
{
    QVector<SeriesData> paths;
    QString error;
};
// Use this view with exportDataCsv to retain units in the standalone CSV header.
struct CrossCsvResult
{
    DataRef data;
    QString error;
};
CrossCsvResult crossSpectrumCsvData(const NodeResult &result);
CrossCurveResult crossSpectrumCurves(const NodeResult &result, const QString &column);
} // namespace aster
#endif
