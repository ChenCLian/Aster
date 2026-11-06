// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/task_categories.h"
#include "processing/astronomy_source_tasks.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "math/astronomy/seasonal_insolation.h"
#include "io/astronomy/la2004_reader.h"
#include "math/astronomy/astronomical_curves.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace aster {
namespace {
enum class Curve { Orbital, Etp, Insolation, Season };
TaskDescriptor source(const QString& label)
{
    auto d = task_detail::seriesTransform(label, taskCategory(TaskCategory::Orbits));
    d.inputKind.reset();
    d.parameters = {
        task_detail::number("age_start_ka", "Start age (ka before J2000)", 0, 0, 50000),
        task_detail::number("age_end_ka", "End age (ka before J2000)", 2000, 0, 50000),
        task_detail::number("age_step_ka", "Spacing (ka; native table = 1)", 1, .001, 50000)};
    for(auto& p:d.parameters) { p.group="age"; p.icon="orbit"; }
    return d;
}
NodeResult failure(const QString& code, const QString& message, QJsonObject context = {})
{ return NodeResult::failure(ErrorInfo{code, std::move(context), message}); }
NodeResult generate(Curve kind, const QJsonObject& p, const ExecutionContext& context)
{
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    const double start = p["age_start_ka"].toDouble(qQNaN());
    const double end = p["age_end_ka"].toDouble(qQNaN());
    const double step = p["age_step_ka"].toDouble(qQNaN());
    const double intervals = (end - start) / step;
    if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(step) ||
        start < 0 || end > 50000 || !(end > start) || step < .001 ||
        !std::isfinite(intervals) || intervals <= 0 || intervals > 1000000)
        return failure("ASTRONOMY_GRID", "Use an increasing age interval in 0..50000 ka and 2..1000000 samples",
                       {{"start_ka",start},{"end_ka",end},{"step_ka",step}});
    // Permit only floating-point roundoff at an exact endpoint, not an extra
    // short interval. For example (0.3 - 0.1) / 0.2 is slightly below one.
    const double tolerance = 16 * std::numeric_limits<double>::epsilon() * std::max(1.0, intervals);
    const int count = int(std::floor(intervals + tolerance)) + 1;
    if (count < 2 || count > 1000000)
        return failure("ASTRONOMY_GRID", "The requested grid must contain 2..1000000 samples");
    if (kind == Curve::Season && count > 20000)
        return failure("ASTRONOMY_SEASON_BUDGET", "Seasonal integration supports at most 20000 ages per source", {{"samples",count},{"maximum",20000}});
    const astronomy::La2004Table* table;
    try { table = &astronomy::la2004(); }
    catch (const std::exception& e) { return failure("ASTRONOMY_DATA", QString::fromUtf8(e.what())); }
    QVector<double> ages; ages.reserve(count);
    std::vector<astronomy::OrbitalElements> elements; elements.reserve(count);
    for (int i = 0; i < count; ++i) {
        if ((i & 1023) == 0 && context.cancellation.isCancelled()) return NodeResult::cancelled();
        const double age = std::min(end, std::fma(double(i), step, start));
        ages.push_back(age); elements.push_back(table->at(age));
    }
    auto series = [&](QString name, QString unit) {
        return SeriesData{ages, QVector<double>(count), {"Age before J2000", "ka"}, {name, unit}};
    };
    QMap<QString, DataRef> outputs;
    QJsonObject report{{"orbital_solution", astronomy::la2004Provenance()},
        {"age_epoch", "J2000"}, {"age_positive", "past"}, {"age_unit", "ka"},
        {"samples", count}, {"actual_start_ka", ages.front()}, {"actual_end_ka", ages.back()},
        {"step_ka", step}, {"interpolation", "linear_elements_shortest_arc_perihelion"},
        {"precession_definition", "e*sin(heliocentric_pibar)"}};
    try {
        if (kind == Curve::Orbital) {
            auto e = series("Eccentricity", "1");
            auto t = series("Obliquity", "deg");
            auto prec = series("Climatic precession", "1");
            for (int i = 0; i < count; ++i) {
                e.y[i] = elements[i].eccentricity;
                t.y[i] = elements[i].obliquity / astronomy::radians;
                prec.y[i] = astronomy::climaticPrecession(elements[i]);
            }
            outputs = {{"eccentricity",makeData(std::move(e))}, {"obliquity",makeData(std::move(t))},
                       {"precession",makeData(std::move(prec))}};
        } else if (kind == Curve::Etp) {
            const double we = p["etp_e"].toDouble(qQNaN()), wt = p["etp_t"].toDouble(qQNaN()),
                         wp = p["etp_p"].toDouble(qQNaN());
            const auto values = astronomy::etp(elements, we, wt, wp);
            auto result = series("ETP", "1");
            for (int i = 0; i < count; ++i) result.y[i] = values[i];
            outputs.insert("etp", makeData(std::move(result)));
            report["standardization"] = "selected_output_grid_sample_sd_N_minus_1";
            report["weights"] = QJsonObject{{"eccentricity",we},{"obliquity",wt},{"precession",wp}};
        } else {
            auto result = series("Daily mean insolation", "W/m²");
            const double latitude = p["latitude_deg"].toDouble(qQNaN());
            const double longitude = p["solar_longitude_deg"].toDouble(qQNaN());
            const double solar = p["solar_constant"].toDouble(qQNaN());
            const bool calendar = p["season_basis"].toString() == "calendar_365";
            const int month1=p["month_start"].toInt(),day1=p["day_start"].toInt();
            const int month2=p["month_end"].toInt(),day2=p["day_end"].toInt();
            SeriesData energy, duration;
            if (kind == Curve::Season) {
                energy = series("Seasonal energy", "MJ/m²");
                duration = series("Season duration", "days");
            }
            if (calendar) {
                (void)astronomy::calendarDay365(month1,day1);
                if(kind==Curve::Season)(void)astronomy::calendarDay365(month2,day2);
                report["calendar"]="fixed_365_day_Gregorian_month_lengths_no_leap_days";
                report["equinox_anchor"]="March_21_noon_day_80";
                report["month_start"]=month1;report["day_start"]=day1;
                if(kind==Curve::Season){report["month_end"]=month2;report["day_end"]=day2;}
            }
            report["season_basis"]=calendar ? "calendar_365" : "solar_longitude";
            report["year_days"]=astronomy::tropicalYearDays;
            report["seconds_per_day"]=86400;
            for (int i = 0; i < count; ++i) {
                if ((i & 1023) == 0 && context.cancellation.isCancelled()) return NodeResult::cancelled();
                if (kind == Curve::Season) {
                    if(context.cancellation.isCancelled())return NodeResult::cancelled();
                    if(context.progress && i%32==0)context.progress(i*100/count,"progress.seasonal_insolation");
                    const auto cancel=[&]{return context.cancellation.isCancelled();};
                    const auto value=calendar
                        ? astronomy::integrateCalendarSeason(elements[i],latitude*astronomy::radians,month1,day1,month2,day2,solar,cancel)
                        : astronomy::integrateSeason(elements[i],latitude*astronomy::radians,
                            p["season_start_deg"].toDouble()*astronomy::radians,
                            p["season_end_deg"].toDouble()*astronomy::radians,solar,cancel);
                    result.y[i]=value.meanWm2;energy.y[i]=value.energyMJm2;duration.y[i]=value.durationDays;
                } else {
                    const double lambda=calendar
                        ? astronomy::solarLongitudeAtDay(elements[i],astronomy::calendarDay365(month1,day1))
                        : longitude*astronomy::radians;
                    result.y[i]=astronomy::dailyMeanInsolation(elements[i],latitude*astronomy::radians,lambda,solar);
                }
            }
            if(kind==Curve::Season) {
                result.yAxis.name="Seasonal mean insolation";
                outputs.insert("season_mean",makeData(std::move(result)));
                outputs.insert("season_energy",makeData(std::move(energy)));
                outputs.insert("season_duration",makeData(std::move(duration)));
                if (!calendar) {
                    report["season_start_deg"] = p["season_start_deg"];
                    report["season_end_deg"] = p["season_end_deg"];
                }
                report["integral"]="time_weighted_adaptive_Simpson_abs_geometry_tolerance_1e-10";
                report["bounds_convention"]="forward_interval_end_exclusive_equal_bounds_full_year";
            } else outputs.insert("insolation", makeData(std::move(result)));
            report["latitude_deg_north"] = latitude;
            if (kind == Curve::Insolation && !calendar)
                report["solar_longitude_deg"] = longitude;
            report["solar_constant_w_m2"] = solar;
            report["season_convention"] = calendar ? "fixed_calendar_with_Kepler_time_of_flight" : "true_solar_longitude_from_moving_vernal_equinox";
            report["insolation_definition"] = kind == Curve::Season
                ? "top_of_atmosphere_time_integral_of_daily_mean_fixed_orbit"
                : "top_of_atmosphere_daily_mean_fixed_orbit";
            report["formula_reference"] = "Berger 1978; IMCCE La2004 insolsub.f cwj/wjour";
        }
    } catch (const std::invalid_argument& e) {
        return failure("ASTRONOMY_PARAMETERS", QString::fromUtf8(e.what()));
    } catch (const std::runtime_error& e) {
        return context.cancellation.isCancelled() ? NodeResult::cancelled()
            : failure("ASTRONOMY_INTEGRATION",QString::fromUtf8(e.what()));
    }
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    auto result = NodeResult::success(std::move(outputs), report);
    if (step < 1) result.warnings << "warning.astronomy_interpolated";
    return result;
}
}
TaskDescriptor OrbitalSourceTask::descriptor()
{
    auto d = source("La2004 orbital curves");
    d.outputs = {{"eccentricity","Eccentricity",DataKind::Series,true},
                 {"obliquity","Obliquity",DataKind::Series,true},
                 {"precession","Climatic precession",DataKind::Series,true}};
    d.defaultOutput = "eccentricity"; return d;
}
TaskDescriptor EtpSourceTask::descriptor()
{
    auto d = source("La2004 ETP composite");
    d.outputs = {{"etp","ETP composite",DataKind::Series,true}}; d.defaultOutput = "etp";
    d.parameters += QVector<ParameterSpec>{
        task_detail::number("etp_e","Eccentricity weight",1,-1000,1000),
        task_detail::number("etp_t","Obliquity weight",1,-1000,1000),
        task_detail::number("etp_p","Precession weight",-1,-1000,1000)};
    return d;
}
TaskDescriptor InsolationSourceTask::descriptor()
{
    auto d = source("La2004 daily mean insolation");
    d.category = taskCategory(TaskCategory::Insolation);
    d.version=2;
    d.outputs = {{"insolation","Daily mean insolation",DataKind::Series,true}}; d.defaultOutput = "insolation";
    d.parameters += QVector<ParameterSpec>{
        task_detail::number("latitude_deg","Latitude (°N; negative = south)",65,-90,90),
        task_detail::number("solar_longitude_deg","Solar longitude (°; 90 = June solstice)",90,0,360),
        task_detail::number("solar_constant","Solar constant at 1 AU (W/m²)",1365,1,10000),
        science_detail::text("season_basis","Season definition","solar_longitude",{"solar_longitude","calendar_365"}),
        task_detail::number("month_start","Month",6,1,12,ParameterKind::Integer),
        task_detail::number("day_start","Day",21,1,31,ParameterKind::Integer)};
    for(auto& p:d.parameters) {
        if(p.group.isEmpty())p.group="season";
        if(p.key=="latitude_deg")p.icon="latitude";
        if(p.key=="solar_longitude_deg") {p.icon="sun";p.visibleWhen={{"season_basis","solar_longitude"}};}
        if(p.key=="month_start"||p.key=="day_start") {p.icon="calendar";p.visibleWhen={{"season_basis","calendar_365"}};}
        if(p.key=="season_basis")p.icon="season";
    }
    return d;
}
TaskDescriptor SeasonalInsolationTask::descriptor()
{
    auto d=InsolationSourceTask::descriptor();d.version=1;d.label="La2004 seasonal insolation";
    d.outputs={{"season_mean","Seasonal mean insolation",DataKind::Series,true},
               {"season_energy","Integrated seasonal energy",DataKind::Series,true},
               {"season_duration","Season duration",DataKind::Series,true}};
    d.defaultOutput="season_mean";
    for(auto it=d.parameters.begin();it!=d.parameters.end();) {
        if(it->key=="solar_longitude_deg")it=d.parameters.erase(it);else ++it;
    }
    for(auto& p:d.parameters)if(p.key=="day_start")p.defaultValue=1;
    d.parameters+=QVector<ParameterSpec>{
        task_detail::number("season_start_deg","Start solar longitude (°)",90,0,360),
        task_detail::number("season_end_deg","End solar longitude (°)",180,0,360),
        task_detail::number("month_end","End month",9,1,12,ParameterKind::Integer),
        task_detail::number("day_end","End day (exclusive)",1,1,31,ParameterKind::Integer)};
    for(auto& p:d.parameters) {
        if(p.group.isEmpty())p.group="season";
        if(p.key=="month_end"||p.key=="day_end") {p.icon="calendar";p.visibleWhen={{"season_basis","calendar_365"}};}
        if(p.key.startsWith("season_")&&p.key.endsWith("_deg")){p.icon="sun";p.visibleWhen={{"season_basis","solar_longitude"}};}
    }
    return d;
}
NodeResult SeasonalInsolationTask::execute(const TaskInput&,const ExecutionContext& c){return generate(Curve::Season,m_parameters,c);}
NodeResult OrbitalSourceTask::execute(const TaskInput&, const ExecutionContext& c) { return generate(Curve::Orbital,m_parameters,c); }
NodeResult EtpSourceTask::execute(const TaskInput&, const ExecutionContext& c) { return generate(Curve::Etp,m_parameters,c); }
NodeResult InsolationSourceTask::execute(const TaskInput&, const ExecutionContext& c) { return generate(Curve::Insolation,m_parameters,c); }
}
