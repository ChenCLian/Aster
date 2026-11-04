#include "processing/chronology_tasks.h"
#include "processing/chronology_helpers.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "processing/task_categories.h"
#include "math/chronology/age_model.h"
#include "io/files/series_file_reader.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <new>
namespace aster
{
namespace
{
constexpr qint64 maxControlBytes = 4 * 1024 * 1024;
using namespace chronology_detail;
} // namespace
TaskDescriptor SedrateToAgeTask::descriptor()
{
    auto d = task_detail::seriesTransform("Sedimentation rate to age model",
                                          taskCategory(TaskCategory::Chronology));
    d.outputs = {{"age_model", "Depth-age model", DataKind::Series, true}};
    d.defaultOutput = "age_model";
    d.parameters = units();
    auto rate =
        science_detail::text("chrono_rate_unit", "Rate unit", "cm/ka",
                             {"cm/ka", "m/ka", "mm/ka", "cm/yr", "m/yr", "mm/yr", "cm/Ma", "m/Ma"});
    rate.group = "chronology_units";
    d.parameters << rate;
    auto start = task_detail::number("chrono_first_age", "Age at first depth", 0, -1e12, 1e12);
    auto rule = science_detail::text("chrono_rate_rule", "Between samples", "left",
                                     {"left", "linear_rate"});
    start.group = rule.group = "rate_integration";
    d.parameters << start << rule;
    return d;
}
NodeResult SedrateToAgeTask::execute(const TaskInput &input, const ExecutionContext &c)
{
    return checked(input, c,
                   [&](const SeriesData &s)
                   {
                       const auto depth = m_parameters["chrono_depth_unit"].toString(),
                                  age = m_parameters["chrono_age_unit"].toString();
                       const auto rate = m_parameters["chrono_rate_unit"].toString();
                       auto mismatch = unitMismatch(s.xAxis.unit, depth);
                       if (!mismatch.isEmpty())
                           return NodeResult::failure(mismatch);
                       mismatch = unitMismatch(s.yAxis.unit, rate, true);
                       if (!mismatch.isEmpty())
                           return NodeResult::failure(mismatch);
                       const auto rateParts = rate.split('/');
                       if (rateParts.size() != 2)
                           throw std::invalid_argument("Invalid sedimentation rate unit");
                       const double factor =
                           metres(depth) / metres(rateParts[0]) * years(rateParts[1]) / years(age);
                       const bool linear = m_parameters["chrono_rate_rule"] == "linear_rate";
                       if (c.progress)
                           c.progress(5, "progress.chronology_rate");
                       auto r =
                           chronology::integrateRate(values(s.x), values(s.y), factor,
                                                     m_parameters["chrono_first_age"].toDouble(),
                                                     linear ? chronology::RateRule::LinearRate
                                                            : chronology::RateRule::LeftConstant,
                                                     [&] { return c.cancellation.isCancelled(); });
                       SeriesData model{s.x, vector(r.age), {"Depth", depth}, {"Age", age}};
                       auto result = NodeResult::success(
                           {{"age_model", makeData(std::move(model))}},
                           {{"method", "integrate_inverse_sedimentation_rate"},
                            {"depth_unit", depth},
                            {"rate_unit", rate},
                            {"age_unit", age},
                            {"age_reference", m_parameters["chrono_age_reference"]},
                            {"age_increases_with_depth", true},
                            {"unit_factor", factor},
                            {"integration_rule", linear ? "linear_rate_exact_reciprocal_integral"
                                                        : "piecewise_constant_left_endpoint"},
                            {"first_age", r.age.front()},
                            {"last_age", r.age.back()},
                            {"minimum_increment", r.minimumIncrement},
                            {"maximum_increment", r.maximumIncrement},
                            {"rows", double(r.age.size())},
                            {"final_rate_used", linear},
                            {"final_rate_validated", true},
                            {"sorted", false},
                            {"resampled", false}});
                       result.warnings << "warning.chronology_deterministic";
                       if (!linear)
                           result.warnings << "warning.chronology_left_rate";
                       if (s.xAxis.unit.isEmpty() || s.yAxis.unit.isEmpty())
                           result.warnings << "warning.chronology_declared_units";
                       if (c.progress)
                           c.progress(100, "progress.chronology_rate");
                       return result;
                   });
}
TaskDescriptor ApplyAgeModelTask::descriptor()
{
    auto d =
        task_detail::seriesTransform("Apply age model", taskCategory(TaskCategory::Chronology));
    d.outputs = {{"aged_series", "Series on age axis", DataKind::Series, true},
                 {"age_model", "Control points used", DataKind::Series, true},
                 {"age_intervals", "Model intervals", DataKind::Table, true}};
    d.defaultOutput = "aged_series";
    auto file = science_detail::text("chrono_model_file", "Depth-age table", "");
    file.fileFilter = "*.txt *.csv *.tsv *.dat";
    auto delimiter = science_detail::text("chrono_delimiter", "Delimiter", "auto",
                                          {"auto", "comma", "tab", "whitespace"});
    auto header = science_detail::text("chrono_header", "Header", "auto", {"auto", "yes", "no"});
    auto x = task_detail::number("chrono_depth_column", "Depth column", 1, 1, 10000,
                                 ParameterKind::Integer);
    auto y =
        task_detail::number("chrono_age_column", "Age column", 2, 1, 10000, ParameterKind::Integer);
    for (auto *param : {&file, &delimiter, &header, &x, &y})
        param->group = "chronology_controls";
    d.parameters = {file, delimiter, header, x, y};
    d.parameters += units();
    auto depth = science_detail::text("chrono_model_depth_unit", "Control depth unit", "m",
                                      {"m", "dm", "cm", "mm", "ft", "km"});
    depth.group = "chronology_units";
    d.parameters << depth;
    auto boundary = science_detail::text("chrono_boundary", "Outside control range", "reject",
                                         {"reject", "linear"});
    boundary.group = "chronology_mapping";
    d.parameters << boundary;
    return d;
}
NodeResult ApplyAgeModelTask::execute(const TaskInput &input, const ExecutionContext &c)
{
    return checked(
        input, c,
        [&](const SeriesData &s)
        {
            const auto depth = m_parameters["chrono_depth_unit"].toString(),
                       age = m_parameters["chrono_age_unit"].toString();
            const auto controlUnit = m_parameters["chrono_model_depth_unit"].toString();
            const auto mismatch = unitMismatch(s.xAxis.unit, depth);
            if (!mismatch.isEmpty())
                return NodeResult::failure(mismatch);
            const auto path = m_parameters["chrono_model_file"].toString();
            if (path.trimmed().isEmpty())
                return NodeResult::failure(
                    ErrorInfo{"CHRONOLOGY_MODEL_FILE", {}, "Choose a depth-age control table"});
            QFile file(path);
            const QFileInfo info(file);
            if (!info.isFile() || !file.open(QIODevice::ReadOnly))
                return NodeResult::failure(
                    makeError(ErrorCode::FileOpen, {{"path", path}}, file.errorString()));
            if (file.size() > maxControlBytes)
                return NodeResult::failure(
                    ErrorInfo{"CHRONOLOGY_BUDGET",
                              {{"path", path}, {"maximum_bytes", double(maxControlBytes)}},
                              "Control file exceeds 4 MiB"});
            if (c.progress)
                c.progress(5, "progress.chronology_controls");
            QByteArray bytes;
            while (!file.atEnd())
            {
                if (c.cancellation.isCancelled())
                    return NodeResult::cancelled();
                const auto chunk =
                    file.read(std::min<qint64>(262144, maxControlBytes + 1 - bytes.size()));
                if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
                    return NodeResult::failure(
                        makeError(ErrorCode::FileRead, {{"path", path}}, file.errorString()));
                bytes += chunk;
                if (bytes.size() > maxControlBytes)
                    return NodeResult::failure(
                        ErrorInfo{"CHRONOLOGY_BUDGET",
                                  {{"maximum_bytes", double(maxControlBytes)}},
                                  "Control file grew beyond 4 MiB"});
            }
            SeriesFileOptions o;
            o.delimiter = m_parameters["chrono_delimiter"].toString();
            o.autoHeader = m_parameters["chrono_header"] == "auto";
            o.header = m_parameters["chrono_header"] == "yes";
            o.xColumn = m_parameters["chrono_depth_column"].toInt() - 1;
            o.yColumn = m_parameters["chrono_age_column"].toInt() - 1;
            if (o.xColumn == o.yColumn)
                return NodeResult::failure(ErrorInfo{
                    "CHRONOLOGY_COLUMNS", {}, "Depth and age must select different columns"});
            o.xUnit = controlUnit;
            o.yUnit = age;
            o.maxRows = chronology::maxControlPoints;
            auto read = readSeriesBytes(bytes, info.absoluteFilePath(), o, c.cancellation);
            if (c.cancellation.isCancelled())
                return NodeResult::cancelled();
            if (!read.ok())
                return NodeResult::failure(
                    ErrorInfo::fromJson(read.report["diagnostic"].toObject()));
            auto controls = *dataAs<SeriesData>(read.data);
            const double conversion = metres(controlUnit) / metres(depth);
            for (qsizetype i = 0; i < controls.x.size(); ++i)
            {
                if ((i & 1023) == 0 && c.cancellation.isCancelled())
                    return NodeResult::cancelled();
                controls.x[i] *= conversion;
            }
            controls.xAxis = {"Depth", depth};
            controls.yAxis = {"Age", age};
            const bool extrapolate = m_parameters["chrono_boundary"] == "linear";
            if (c.progress)
                c.progress(30, "progress.chronology_mapping");
            auto r = chronology::mapDepth(values(s.x), values(controls.x), values(controls.y),
                                          extrapolate ? chronology::Boundary::Linear
                                                      : chronology::Boundary::Reject,
                                          [&] { return c.cancellation.isCancelled(); });
            SeriesData output{vector(r.age), s.y, {"Age", age}, s.yAxis};
            TableData intervals;
            intervals.columns = {"Depth start (" + depth + ")", "Depth end (" + depth + ")",
                                 "Age start (" + age + ")", "Age end (" + age + ")",
                                 "Interval rate (" + depth + "/" + age + ")"};
            intervals.rows.reserve(controls.x.size() - 1);
            for (qsizetype i = 0; i + 1 < controls.x.size(); ++i)
            {
                if ((i & 1023) == 0 && c.cancellation.isCancelled())
                    return NodeResult::cancelled();
                intervals.rows.push_back({controls.x[i], controls.x[i + 1], controls.y[i],
                                          controls.y[i + 1], r.intervalRate[std::size_t(i)]});
            }
            read.report["sha256"] = QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            read.report["snapshot_scope"] = "captured_bytes_per_node_execution";
            const double minimum = controls.x.front(), maximum = controls.x.back();
            auto result = NodeResult::success(
                {{"aged_series", makeData(std::move(output))},
                 {"age_model", makeData(std::move(controls))},
                 {"age_intervals", makeData(std::move(intervals))}},
                {{"method", "piecewise_linear_depth_to_age"},
                 {"control_file", read.report},
                 {"depth_unit", depth},
                 {"age_unit", age},
                 {"age_reference", m_parameters["chrono_age_reference"]},
                 {"age_increases_with_depth", true},
                 {"control_depth_to_input_factor", conversion},
                 {"control_depth_min", minimum},
                 {"control_depth_max", maximum},
                 {"boundary", extrapolate ? "linear_endpoint_secant" : "reject"},
                 {"extrapolated_samples", double(r.extrapolated)},
                 {"rows", double(s.x.size())},
                 {"resampled", false},
                 {"proxy_changed", false},
                 {"sorted", false}});
            result.warnings << "warning.chronology_deterministic"
                            << "warning.chronology_no_resampling"
                            << "warning.chronology_control_snapshot";
            if (s.xAxis.unit.isEmpty())
                result.warnings << "warning.chronology_declared_units";
            if (r.extrapolated)
                result.warnings << "warning.chronology_extrapolated";
            if (c.progress)
                c.progress(100, "progress.chronology_mapping");
            return result;
        });
}
} // namespace aster
