// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core/task_registry.h"
namespace aster
{
// Returns zero if the port is not a measured, one-sided PSD with known resolution.
double observedPsdResolution(const InputOrigin &origin);
class BandPowerTask final : public Task
{
  public:
    explicit BandPowerTask(const QJsonObject &parameters) : m_parameters(parameters) {}
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput &, const ExecutionContext &) override;
  private:
    QJsonObject m_parameters;
};
}
