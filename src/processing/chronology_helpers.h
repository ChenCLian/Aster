#ifndef ASTER_PROCESSING_CHRONOLOGY_HELPERS_H
#define ASTER_PROCESSING_CHRONOLOGY_HELPERS_H
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "math/chronology/age_model.h"
#include <cmath>
#include <new>
namespace aster::chronology_detail
{
inline QString canonicalLength(QString s)
{
    s = s.trimmed().toLower();
    return QStringList{"m", "dm", "cm", "mm", "km", "ft"}.contains(s) ? s : QString();
}
inline QString canonicalTime(QString s)
{
    s = s.trimmed().toLower();
    if (s == "yr" || s == "a" || s == "year" || s == "years")
        return "yr";
    if (s == "ka" || s == "kyr" || s == "kyrs")
        return "ka";
    if (s == "ma" || s == "myr" || s == "myrs")
        return "Ma";
    return {};
}
inline QString canonicalRate(QString s)
{
    s.remove(' ');
    const auto parts = s.split('/');
    if (parts.size() != 2)
        return {};
    const auto length = canonicalLength(parts[0]), time = canonicalTime(parts[1]);
    return length.isEmpty() || time.isEmpty() ? QString() : length + "/" + time;
}
inline double metres(const QString &s)
{
    if (s == "m")
        return 1;
    if (s == "dm")
        return .1;
    if (s == "cm")
        return .01;
    if (s == "mm")
        return .001;
    if (s == "km")
        return 1000;
    if (s == "ft")
        return .3048;
    throw std::invalid_argument("Unknown depth unit");
}
inline double years(const QString &s)
{
    if (s == "yr")
        return 1;
    if (s == "ka")
        return 1000;
    if (s == "Ma")
        return 1000000;
    throw std::invalid_argument("Unknown age unit");
}
inline ErrorInfo unitMismatch(const QString &actual, const QString &declared, bool rate = false)
{
    if (actual.trimmed().isEmpty())
        return {};
    const auto canonical = rate ? canonicalRate(actual) : canonicalLength(actual);
    if (canonical == declared)
        return {};
    return {
        "CHRONOLOGY_UNIT",
        {{"input_unit", actual}, {"declared_unit", declared}},
        "Declared units must match nonempty input metadata; units are never silently relabelled"};
}
inline std::vector<double> values(const QVector<double> &v)
{
    return {v.begin(), v.end()};
}
inline QVector<double> vector(const std::vector<double> &v)
{
    return {v.begin(), v.end()};
}
inline QVector<ParameterSpec> units()
{
    auto depth = science_detail::text("chrono_depth_unit", "Input depth unit", "m",
                                      {"m", "dm", "cm", "mm", "ft", "km"});
    auto age = science_detail::text("chrono_age_unit", "Age unit", "ka", {"yr", "ka", "Ma"});
    auto reference = science_detail::text("chrono_age_reference", "Age reference", "unspecified",
                                          {"unspecified", "BP1950", "J2000", "relative"});
    depth.group = age.group = reference.group = "chronology_units";
    return {depth, age, reference};
}
template <class F>
NodeResult checked(const TaskInput &input, const ExecutionContext &c, F operation)
{
    if (c.cancellation.isCancelled())
        return NodeResult::cancelled();
    const auto *s = dataAs<SeriesData>(input.data);
    if (!s)
        return NodeResult::failure(makeError(ErrorCode::InputType, {{"expected", "Series"}}));
    const auto invalid = validateData(input.data);
    if (!invalid.isEmpty())
        return NodeResult::failure(makeError(ErrorCode::InputInvalid, {}, invalid));
    const auto order = science_detail::orderedSeries(*s);
    if (!order.isEmpty())
        return NodeResult::failure(order);
    if (std::size_t(s->x.size()) > chronology::maxSamples)
        return NodeResult::failure(ErrorInfo{"CHRONOLOGY_BUDGET",
                                             {{"maximum", double(chronology::maxSamples)}},
                                             "Chronology sample limit exceeded"});
    try
    {
        return operation(*s);
    }
    catch (const chronology::Cancelled &)
    {
        return NodeResult::cancelled();
    }
    catch (const chronology::Failure &e)
    {
        QString code;
        using P = chronology::Problem;
        switch (e.problem)
        {
        case P::Shape:
            code = "CHRONOLOGY_SHAPE";
            break;
        case P::DepthOrder:
            code = "CHRONOLOGY_DEPTH_ORDER";
            break;
        case P::AgeOrder:
            code = "CHRONOLOGY_AGE_ORDER";
            break;
        case P::RatePositive:
            code = "CHRONOLOGY_RATE_POSITIVE";
            break;
        case P::OutsideModel:
            code = "CHRONOLOGY_COVERAGE";
            break;
        case P::NumericRange:
            code = "CHRONOLOGY_NUMERIC";
            break;
        case P::CycleAnchors:
            code = "CYCLE_ANCHORS";
            break;
        case P::Budget:
            code = "CHRONOLOGY_BUDGET";
            break;
        }
        QJsonObject context;
        if (e.row)
            context.insert("point", double(e.row));
        return NodeResult::failure(ErrorInfo{code, context, QString::fromUtf8(e.what())});
    }
    catch (const std::bad_alloc &)
    {
        return NodeResult::failure(makeError(ErrorCode::AllocationFailure));
    }
    catch (const std::exception &e)
    {
        return NodeResult::failure(
            ErrorInfo{"CHRONOLOGY_PARAMETERS", {}, QString::fromUtf8(e.what())});
    }
}
} // namespace aster::chronology_detail
#endif
