// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/task_categories.h"
#include "processing/sine_source_task.h"
#include "processing/task_helpers.h"
#include <algorithm>
#include <cmath>

namespace aster {
using namespace task_detail;
TaskDescriptor SineSourceTask::descriptor()
{
    auto d = seriesTransform("Sine signal", taskCategory(TaskCategory::TestSignals));
    d.inputKind.reset();
    d.parameters = {
        number("count", "Sample count", 128, 2, 1000000, ParameterKind::Integer),
        number("step", "Sample spacing", 0.25, 1e-9, 1e6),
        number("period", "Period", 4.0, 1e-9, 1e9),
        number("amplitude", "Amplitude", 2.0, -1e12, 1e12),
        number("slope", "Linear slope", 0.15, -1e12, 1e12)
    };
    return d;
}
SineSourceTask::SineSourceTask(const QJsonObject& p)
    : m_count(p.value("count").toInt()), m_step(p.value("step").toDouble()),
      m_period(p.value("period").toDouble()), m_amplitude(p.value("amplitude").toDouble()),
      m_slope(p.value("slope").toDouble()) {}
NodeResult SineSourceTask::execute(const TaskInput&, const ExecutionContext& context)
{
    SeriesData data;
    data.x.resize(m_count); data.y.resize(m_count);
    data.xAxis = {"Time", "arbitrary"}; data.yAxis = {"Signal", "arbitrary"};
    constexpr double twoPi = 6.283185307179586476925286766559;
    for (int i = 0; i < m_count; ++i) {
        if (cancelled(context, i)) return NodeResult::cancelled();
        data.x[i] = i * m_step;
        data.y[i] = m_amplitude * std::sin(twoPi * data.x[i] / m_period) + m_slope * data.x[i];
    }
    return NodeResult::success({{"series", makeData(std::move(data))}}, {{"samples", m_count}});
}

} // namespace aster
