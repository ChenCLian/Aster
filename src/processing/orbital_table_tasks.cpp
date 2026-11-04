#include "processing/task_categories.h"
#include "processing/orbital_table_tasks.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "io/astronomy/orbital_table.h"
#include "math/astronomy/orbital_elements.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace aster {
namespace {
TaskDescriptor descriptor(const QString& label, bool pt)
{
    auto d = task_detail::seriesTransform(label,taskCategory(TaskCategory::Orbits));
    d.inputKind.reset();
    if (pt) {
        auto path = science_detail::text("pt_table_path","PT data table (optional)","");
        path.fileFilter = "PT.De*Td*.dat"; path.group = "orbital_solution"; path.icon = "orbit";
        d.parameters.push_back(path);
    } else {
        auto choice = science_detail::text("eccentricity_solution","Orbital solution","la2010a",
                                           astronomy::eccentricitySolutionIds());
        choice.group = "orbital_solution"; choice.icon = "orbit";
        d.parameters.push_back(choice);
    }
    const double maximum = pt ? 100000 : 300000;
    d.parameters += QVector<ParameterSpec>{
        task_detail::number("orbit_start_ka","Start age (ka before J2000)",0,0,maximum),
        task_detail::number("orbit_end_ka","End age (ka before J2000)",2000,0,maximum),
        task_detail::number("orbit_step_ka","Output spacing (ka)",1,.001,maximum)};
    for (auto& parameter:d.parameters) if (parameter.group.isEmpty()) {
        parameter.group = "age"; parameter.icon = "orbit";
    }
    return d;
}
NodeResult generate(const QJsonObject& p, const ExecutionContext& context, bool pt)
{
    const auto error = [](QString code,QString message,QJsonObject details = {}) {
        return NodeResult::failure(ErrorInfo{std::move(code),std::move(details),std::move(message)});
    };
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    const double start = p["orbit_start_ka"].toDouble(qQNaN());
    const double end = p["orbit_end_ka"].toDouble(qQNaN());
    const double step = p["orbit_step_ka"].toDouble(qQNaN());
    const double intervals = (end-start)/step;
    if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(step) ||
        start < 0 || end > 300000 || end <= start || step < .001 ||
        !std::isfinite(intervals) || intervals <= 0 || intervals > 999999)
        return error("ORBITAL_GRID","Use an increasing age grid with 2..1000000 samples",
                     {{"start_ka",start},{"end_ka",end},{"step_ka",step}});
    const int count = int(std::floor(intervals+16*std::numeric_limits<double>::epsilon()*std::max(1.,intervals)))+1;
    if (count < 2 || count > 1000000) return error("ORBITAL_GRID","Orbital output exceeds 1000000 samples");
    astronomy::OrbitalDataset dataset;
    try {
        if (context.progress) context.progress(0,"progress.orbital_table");
        dataset = pt ? astronomy::precessionTiltDataset(p["pt_table_path"].toString())
                     : astronomy::orbitalDataset(p["eccentricity_solution"].toString());
    } catch (const std::exception& exception) {
        return context.cancellation.isCancelled() ? NodeResult::cancelled()
            : error("ORBITAL_DATA",QString::fromUtf8(exception.what()),
                    {{"solution",pt?"pt_zb18a":p["eccentricity_solution"].toString()},
                     {"path",p["pt_table_path"].toString()}});
    }
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    if (end > dataset.table->maximumAge())
        return error("ORBITAL_RANGE","Requested age exceeds this table; extrapolation is disabled",
                     {{"end_ka",end},{"maximum_age_ka",dataset.table->maximumAge()},
                      {"solution",dataset.provenance["id"]}});
    QVector<double> ages; ages.reserve(count);
    for (int i=0;i<count;++i) ages.push_back(std::min(end,std::fma(double(i),step,start)));
    QMap<QString,DataRef> outputs;
    const auto produce = [&](astronomy::OrbitalQuantity quantity,const QString& port,const QString& name,
                             const QString& unit,double scale = 1.) {
        SeriesData series{ages,QVector<double>(count),{"Age before J2000","ka"},{name,unit}};
        for (int i=0;i<count;++i) {
            if ((i&1023)==0 && context.cancellation.isCancelled()) return false;
            series.y[i] = dataset.table->at(ages[i],quantity)*scale;
        }
        outputs.insert(port,makeData(std::move(series))); return true;
    };
    try {
        if (pt) {
            if (!produce(astronomy::OrbitalQuantity::Obliquity,"obliquity","Obliquity","deg",1/astronomy::radians) ||
                !produce(astronomy::OrbitalQuantity::ClimaticPrecession,"precession","Climatic precession","1") ||
                !produce(astronomy::OrbitalQuantity::AxialPrecession,"axial_precession","Axial precession from ECLIPJ2000","rad"))
                return NodeResult::cancelled();
        } else if (!produce(astronomy::OrbitalQuantity::Eccentricity,"eccentricity","Eccentricity","1"))
            return NodeResult::cancelled();
    } catch (const std::exception& exception) {
        return error("ORBITAL_DATA",QString::fromUtf8(exception.what()));
    }
    QJsonObject report{{"orbital_solution",dataset.provenance},{"age_epoch","J2000"},{"age_positive","past"},
        {"age_unit","ka"},{"samples",count},{"actual_start_ka",ages.front()},{"actual_end_ka",ages.back()},
        {"step_ka",step},{"native_min_step_ka",dataset.table->minimumSpacing()},
        {"native_max_step_ka",dataset.table->maximumSpacing()},
        {"interpolation","linear_scalar_channels_shortest_arc_axial_angle"},
        {"extrapolation",false}};
    if (pt) {
        report["precession_definition"] = "source_column_4_e_sin_varpi_from_moving_equinox_dimensionless";
        report["axial_precession_definition"] = "source_column_3_angle_from_ECLIPJ2000_not_perihelion";
        report["insolation_supported"] = false;
    }
    auto result = NodeResult::success(std::move(outputs),std::move(report));
    result.warnings << "warning.orbital_resampling";
    if (pt) result.warnings << "warning.pt_model";
    if (pt && !p["pt_table_path"].toString().trimmed().isEmpty()) result.warnings << "warning.pt_external";
    const double confidence = dataset.provenance["phase_caution_after_ka"].toDouble(-1);
    if (confidence >= 0 && end > confidence) result.warnings << "warning.orbital_deep_time";
    if (context.cancellation.isCancelled()) return NodeResult::cancelled();
    return result;
}
}
TaskDescriptor EccentricitySourceTask::descriptor()
{
    auto d = aster::descriptor("Orbital eccentricity (La2010 / ZB)",false);
    d.outputs = {{"eccentricity","Eccentricity",DataKind::Series,true}};
    d.defaultOutput = "eccentricity"; return d;
}
TaskDescriptor PrecessionTiltSourceTask::descriptor()
{
    auto d = aster::descriptor("PT-ZB18a obliquity and precession",true);
    d.outputs = {{"obliquity","Obliquity",DataKind::Series,true},
                 {"precession","Climatic precession",DataKind::Series,true},
                 {"axial_precession","Axial precession angle",DataKind::Series,true}};
    d.defaultOutput = "precession"; return d;
}
NodeResult EccentricitySourceTask::execute(const TaskInput&,const ExecutionContext& c)
{ return generate(m_parameters,c,false); }
NodeResult PrecessionTiltSourceTask::execute(const TaskInput&,const ExecutionContext& c)
{ return generate(m_parameters,c,true); }
}
