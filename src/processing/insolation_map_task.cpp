#include "processing/insolation_map_task.h"
#include "processing/astronomy_source_tasks.h"
#include "processing/task_helpers.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace aster {
TaskDescriptor InsolationMapTask::descriptor() {
    // One authoritative daily-insolation parameter/default contract. The map
    // changes only the latitude grid and output type, never the solar formula.
    auto d=InsolationSourceTask::descriptor();d.version=1;d.label="La2004 latitude–age insolation map";
    d.outputs={{"insolation_map","Latitude–age insolation",DataKind::Grid,true}};
    d.defaultOutput="insolation_map";
    for(auto it=d.parameters.begin();it!=d.parameters.end();) {
        if(it->key=="latitude_deg")it=d.parameters.erase(it);else ++it;
    }
    const QVector<ParameterSpec> latitudes={
        task_detail::number("latitude_start_deg","South latitude (°N)",-90,-90,90),
        task_detail::number("latitude_end_deg","North latitude (°N)",90,-90,90),
        task_detail::number("latitude_step_deg","Latitude spacing (°)",5,.1,180)};
    for(auto p:latitudes){p.group="latitude_grid";p.icon="latitude";d.parameters.push_back(p);}
    return d;
}
NodeResult InsolationMapTask::execute(const TaskInput&,const ExecutionContext& c) {
    if(c.cancellation.isCancelled())return NodeResult::cancelled();
    auto error=[](QString code,QString text,QJsonObject context={}){return NodeResult::failure(ErrorInfo{code,context,text});};
    const double lo=m_parameters["latitude_start_deg"].toDouble(qQNaN()),hi=m_parameters["latitude_end_deg"].toDouble(qQNaN()),
                 step=m_parameters["latitude_step_deg"].toDouble(qQNaN());
    const double intervals=(hi-lo)/step;
    const double ageIntervals=(m_parameters["age_end_ka"].toDouble(qQNaN())-m_parameters["age_start_ka"].toDouble(qQNaN()))/
                              m_parameters["age_step_ka"].toDouble(qQNaN());
    if(!std::isfinite(intervals) || !std::isfinite(lo) || !std::isfinite(hi) || !std::isfinite(step) ||
       lo < -90 || hi > 90 || !(hi>lo) || step<.1 || intervals<1)
        return error("INSOLATION_MAP_GRID","Use at least two increasing latitudes in [-90,90] with positive spacing");
    const auto count=[](double n){return std::floor(n+16*std::numeric_limits<double>::epsilon()*std::max(1.,n))+1;};
    const double rows=count(intervals),columns=std::isfinite(ageIntervals)?count(ageIntervals):0;
    if(rows>361 || columns>20000 || columns<2 || rows*columns>2000000)
        return error("INSOLATION_MAP_BUDGET","Map budget: 361 latitudes, 20000 ages, 2000000 cells",
                     {{"latitudes",rows},{"ages",columns},{"maximum_cells",2000000}});
    GridData grid;grid.rows=qsizetype(rows);grid.columns=qsizetype(columns);
    grid.yAxis={"Latitude","°N"};grid.valueAxis={"Daily mean insolation","W/m²"};
    grid.values.reserve(grid.rows*grid.columns);grid.y.reserve(grid.rows);
    QJsonObject report;QStringList warnings;
    auto parameters=m_parameters;
    parameters.remove("latitude_start_deg");parameters.remove("latitude_end_deg");parameters.remove("latitude_step_deg");
    for(qsizetype row=0;row<grid.rows;++row) {
        if(c.cancellation.isCancelled())return NodeResult::cancelled();
        if(c.progress)c.progress(int(row*100/grid.rows),"progress.insolation_map");
        const double latitude=std::min(hi,std::fma(double(row),step,lo));
        parameters["latitude_deg"]=latitude;
        const auto r=InsolationSourceTask(parameters).execute({},c);
        if(r.state!=NodeState::Succeeded)return r;
        const auto* series=dataAs<SeriesData>(r.outputs.value("insolation"));
        if(!series || series->y.size()!=grid.columns)
            return NodeResult::failure(makeError(ErrorCode::OutputInvalid,{},"Insolation row/grid contract mismatch"));
        if(row==0){grid.x=series->x;grid.xAxis=series->xAxis;report=r.report;warnings=r.warnings;}
        grid.y.push_back(latitude);grid.values+=series->y;
    }
    if(c.cancellation.isCancelled())return NodeResult::cancelled();
    report.remove("latitude_deg_north");report["latitude_start_deg"]=grid.y.front();report["latitude_end_deg"]=grid.y.back();
    report["latitude_step_deg"]=step;report["latitudes"]=double(grid.rows);report["cells"]=double(grid.values.size());
    report["layout"]="rows_latitude_columns_age";
    auto result=NodeResult::success({{"insolation_map",makeData(std::move(grid))}},report);result.warnings=warnings;return result;
}
}
