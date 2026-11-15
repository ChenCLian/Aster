// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/outlier_tasks.h"
#include "processing/task_categories.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
namespace aster {
namespace {
constexpr qsizetype maxSamples=200000;
constexpr qint64 maxWindowWork=20000000;
long double median(std::vector<long double> &values)
{
    const auto n=values.size(); const auto middle=values.begin()+n/2;
    std::nth_element(values.begin(),middle,values.end());
    if (n%2) return *middle;
    return (*std::max_element(values.begin(),middle))/2+(*middle)/2;
}
QString fingerprint(const SeriesData &data,const ExecutionContext &context)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto *values : {&data.x,&data.y})
        for (qsizetype start=0;start<values->size();start+=1024) {
            if (context.cancellation.isCancelled()) return {};
            QByteArray block; QDataStream stream(&block,QIODevice::WriteOnly);
            stream.setByteOrder(QDataStream::LittleEndian); stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
            for (qsizetype i=start;i<std::min(start+1024,values->size());++i) stream<<(*values)[i];
            hash.addData(block);
        }
    return QString::fromLatin1(hash.result().toHex());
}
NodeResult invalid(const QString &message)
{ return NodeResult::failure(makeError(ErrorCode::ParameterInvalid,{},message)); }
}
TaskDescriptor OutlierTask::descriptor()
{
    auto d=task_detail::seriesTransform("Outlier handling",taskCategory(TaskCategory::Cleanup));
    d.outputs.push_back({"original","Original series",DataKind::Series,true});
    d.outputs.push_back({"outliers","Selected outliers",DataKind::Series,false});
    d.outputs.push_back({"outlier_audit","Point decisions",DataKind::Table,true});
    d.parameters={science_detail::text("outlier_method","Detection method","hampel",{"hampel","mad","bounds"}),
        task_detail::number("outlier_window","Local window (points)",11,3,1001,ParameterKind::Integer),
        task_detail::number("outlier_threshold","MAD threshold",3,0.1,1000),
        task_detail::number("outlier_lower","Lower Y bound",-1e100,-1e100,1e100),
        task_detail::number("outlier_upper","Upper Y bound",1e100,-1e100,1e100),
        science_detail::text("outlier_action","Detected points","mark",{"mark","exclude","median"}),
        science_detail::text("point_overrides","Point decisions","",{})};
    for(auto &p:d.parameters) {
        p.group="outlier_handling";
        if(p.key=="point_overrides") { p.editorHidden=true; p.perDatasetOnly=true; }
        if(p.key=="outlier_lower" || p.key=="outlier_upper") p.visibleWhen={{"outlier_method","bounds"}};
    }
    return d;
}
NodeResult OutlierTask::execute(const TaskInput &input,const ExecutionContext &context)
{
    const auto *source=dataAs<SeriesData>(input.data);
    if(!source) return NodeResult::failure(makeError(ErrorCode::InputType,{{"expected","Series"}}));
    if(context.cancellation.isCancelled()) return NodeResult::cancelled();
    const auto error=validateData(input.data); if(!error.isEmpty()) return invalid(error);
    const auto ordered=science_detail::orderedSeries(*source); if(!ordered.isEmpty()) return NodeResult::failure(ordered);
    const auto n=source->y.size();
    const auto method=m_parameters["outlier_method"].toString(), action=m_parameters["outlier_action"].toString();
    const int window=m_parameters["outlier_window"].toInt();
    const double lower=m_parameters["outlier_lower"].toDouble(), upper=m_parameters["outlier_upper"].toDouble();
    const double threshold=m_parameters["outlier_threshold"].toDouble();
    if(window<3 || window%2==0) return invalid("Local window must contain an odd number of points (at least 3)");
    if(lower>upper) return invalid("Lower bound must not exceed upper bound");
    if(n>maxSamples || ((method=="hampel" || action=="median") && qint64(n)*std::min<qsizetype>(n,window)>maxWindowWork))
        return invalid("Outlier analysis exceeds the sample/window budget; reduce the window or select a smaller interval");
    const auto identity=fingerprint(*source,context); if(context.cancellation.isCancelled()) return NodeResult::cancelled();
    QMap<int,bool> decisions;
    bool stale=false;
    const auto raw=m_parameters["point_overrides"].toString();
    if(!raw.isEmpty()) {
        QJsonParseError parse; const auto decoded=QJsonDocument::fromJson(raw.toUtf8(),&parse);
        if(parse.error!=QJsonParseError::NoError || !decoded.isObject()) return invalid("Malformed point decisions");
        const auto object=decoded.object(); const auto points=object["points"].toObject();
        if(!object["points"].isObject() || points.size()>5000) return invalid("Invalid or oversized point decisions");
        stale=object["fingerprint"].toString()!=identity;
        if(!stale) for(auto it=points.begin();it!=points.end();++it) {
            bool ok=false; const int index=it.key().toInt(&ok);
            if(!ok || index<0 || index>=n || !it.value().isBool()) return invalid("Invalid point decision index");
            decisions[index]=it.value().toBool();
        }
    }
    std::vector<long double> scratch, deviations;
    long double globalMedian=0,globalMad=0;
    if(method=="mad") {
        scratch.assign(source->y.begin(),source->y.end()); globalMedian=median(scratch);
        deviations.reserve(n); for(auto value:source->y) deviations.push_back(std::abs(static_cast<long double>(value)-globalMedian));
        globalMad=median(deviations); scratch.clear(); deviations.clear();
    }
    SeriesData output,marked;
    output.xAxis=marked.xAxis=source->xAxis; output.yAxis=marked.yAxis=source->yAxis;
    TableData audit{{"index","x","original_value","result_value","detected","selected","action","score"},{}};
    output.x.reserve(n); output.y.reserve(n); audit.rows.reserve(n);
    int detectedCount=0,selectedCount=0,excludedCount=0,replacedCount=0;
    for(qsizetype i=0;i<n;++i) {
        if(i%128==0 && context.cancellation.isCancelled()) return NodeResult::cancelled();
        long double center=globalMedian, mad=globalMad;
        if(method=="hampel" || action=="median") {
            scratch.clear();
            const auto first=std::max<qsizetype>(0,i-window/2), last=std::min<qsizetype>(n,i+window/2+1);
            for(auto j=first;j<last;++j) scratch.push_back(source->y[j]);
            const auto local=median(scratch);
            if(method=="hampel") {
                deviations.clear(); for(auto value:scratch) deviations.push_back(std::abs(value-local)); mad=median(deviations);
            }
            if(method=="hampel" || action=="median") center=local;
        }
        // Global MAD detection always uses the global center, even if replacement is local.
        const long double detectionCenter=method=="mad" ? globalMedian : center;
        const auto difference=std::abs(static_cast<long double>(source->y[i])-detectionCenter);
        const auto spread=1.482602218505602L*mad;
        const bool detected=method=="bounds" ? source->y[i]<lower || source->y[i]>upper :
            (mad==0 ? difference>0 : difference/spread>threshold);
        bool selected=decisions.contains(int(i)) ? decisions.value(int(i)) : detected;
        detectedCount+=detected; selectedCount+=selected;
        if(selected) { marked.x.push_back(source->x[i]); marked.y.push_back(source->y[i]); }
        const bool explicitExclude=decisions.contains(int(i)) && decisions.value(int(i));
        const bool exclude=explicitExclude || (selected && action=="exclude");
        const bool replace=!exclude && selected && action=="median";
        const double value=replace ? double(center) : source->y[i];
        if(!std::isfinite(value)) return NodeResult::failure(makeError(ErrorCode::NumericRange));
        if(!exclude) { output.x.push_back(source->x[i]); output.y.push_back(value); }
        excludedCount+=exclude; replacedCount+=replace;
        const long double ratio=spread>0 ? difference/spread : -1;
        const double score=method=="bounds" || ratio>std::numeric_limits<double>::max() ? -1.0 : double(ratio);
        audit.rows.push_back({double(i),source->x[i],source->y[i],exclude ? -1.0 : value,
            detected ? 1.0 : 0.0,selected ? 1.0 : 0.0,exclude ? 1.0 : replace ? 2.0 : 0.0,score});
    }
    if(output.x.isEmpty()) return invalid("All points would be excluded; adjust the detection settings or keep some points");
    auto result=NodeResult::success({{"series",makeData(std::move(output))},{"original",input.data},{"outlier_audit",makeData(std::move(audit))}},
        {{"outlier_method",method},{"outlier_action",action},{"outlier_input_fingerprint",identity},
         {"outlier_input_rows",double(n)},{"detected_count",detectedCount},{"selected_count",selectedCount},
         {"excluded_count",excludedCount},{"replaced_count",replacedCount},{"manual_decisions",decisions.size()},
         {"stale_decisions_ignored",stale},{"outlier_window",window},{"outlier_threshold",threshold},{"resampled",false}});
    if(!marked.x.isEmpty()) result.outputs["outliers"]=makeData(std::move(marked));
    if(stale) result.warnings<<"Input changed: previous point decisions were ignored; review the points again";
    return result;
}
TaskDescriptor ValueTransformTask::descriptor()
{
    auto d=task_detail::seriesTransform("Value transformation",taskCategory(TaskCategory::Amplitude));
    d.parameters={science_detail::text("value_transform","Transformation","asinh",{"ln","log10","asinh","tanh"}),
        task_detail::number("transform_scale","Scale (input Y unit)",1,1e-100,1e100)};
    for(auto &p:d.parameters) p.group="amplitude_transform";
    return d;
}
NodeResult ValueTransformTask::execute(const TaskInput &input,const ExecutionContext &context)
{
    const auto *source=dataAs<SeriesData>(input.data);
    if(!source) return NodeResult::failure(makeError(ErrorCode::InputType,{{"expected","Series"}}));
    if(context.cancellation.isCancelled()) return NodeResult::cancelled();
    const auto error=validateData(input.data); if(!error.isEmpty()) return invalid(error);
    if(source->y.size()>1000000) return invalid("Transformation exceeds the sample budget");
    const auto method=m_parameters["value_transform"].toString();
    const double scale=m_parameters["transform_scale"].toDouble();
    if(!std::isfinite(scale) || scale<=0) return invalid("Scale must be positive and finite");
    auto output=*source;
    for(qsizetype i=0;i<output.y.size();++i) {
        if(task_detail::cancelled(context,i)) return NodeResult::cancelled();
        const double value=source->y[i];
        if((method=="ln" || method=="log10") && value<=0)
            return NodeResult::failure(makeError(ErrorCode::ParameterInvalid,{{"point",double(i+1)},{"value",value}},"Logarithms require strictly positive values"));
        double result=0;
        if(method=="ln" || method=="log10") {
            result=std::log(value)-std::log(scale); if(method=="log10") result/=std::log(10.0);
        } else {
            const double ratio=value/scale;
            if(method=="tanh") result=std::tanh(ratio);
            else if(method=="asinh") result=std::isfinite(ratio) ? std::asinh(ratio) :
                std::copysign(std::log(std::abs(value))-std::log(scale)+std::log(2.0),value);
            else return invalid("Unknown transformation");
        }
        if(!std::isfinite(result)) return NodeResult::failure(makeError(ErrorCode::NumericRange));
        output.y[i]=result;
    }
    output.yAxis.unit="1";
    return NodeResult::success({{"series",makeData(std::move(output))}},
        {{"transformation",method},{"scale",scale},{"input_unit",source->yAxis.unit},
         {"formula",method+"(y/scale)"},{"resampled",false},{"input_unchanged",true}});
}
}
