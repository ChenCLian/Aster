// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/task_categories.h"
#include "processing/filter_tasks.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "math/filtering/bandpass.h"
#include <new>
namespace aster {
namespace {
NodeResult fail(const QString& code,const QString& technical,QJsonObject context={}) {
    return NodeResult::failure(ErrorInfo{code,std::move(context),technical});
}
TaskDescriptor filterDescriptor(const QString& label) {
    auto d=task_detail::seriesTransform(label,taskCategory(TaskCategory::Filtering));
    d.outputs={{"filtered","Filtered series",DataKind::Series,true},
               {"envelope","Amplitude envelope",DataKind::Series,true},
               {"filter_response","Amplitude response",DataKind::Series,true}};
    d.defaultOutput="filtered";
    d.parameters={task_detail::number("lower_frequency","Lower frequency",.02,0,1e12),
                  task_detail::number("upper_frequency","Upper frequency",.06,0,1e12)};
    for(auto& p:d.parameters)p.group="filter_band";
    return d;
}
NodeResult executeFilter(const TaskInput& input,const ExecutionContext& c,const QJsonObject& p,
                   bool envelopeOnly,filtering::Shape shape) {
    if(c.cancellation.isCancelled())return NodeResult::cancelled();
    const auto* data=dataAs<SeriesData>(input.data);
    if(!data)return NodeResult::failure(makeError(ErrorCode::InputType,{{"expected","Series"}}));
    const auto invalid=validateData(input.data);
    if(!invalid.isEmpty())return NodeResult::failure(makeError(ErrorCode::InputInvalid,{},invalid));
    const int minimum=envelopeOnly?2:8;
    if(data->x.size()<minimum)return NodeResult::failure(makeError(ErrorCode::TooFewSamples,{{"minimum",minimum},{"actual",double(data->x.size())}}));
    double dt=0;const auto error=science_detail::uniformSpacing(*data,dt);
    if(!error.isEmpty())return NodeResult::failure(error);
    const double low=p["lower_frequency"].toDouble(qQNaN()),high=p["upper_frequency"].toDouble(qQNaN());
    if(!envelopeOnly && (!std::isfinite(low) || !std::isfinite(high) || !(low>0 && high>low) || high>=.5/dt))
        return fail("FILTER_BAND","Require 0 < lower < upper < Nyquist",{{"lower_frequency",low},{"upper_frequency",high},{"nyquist",.5/dt}});
    try {
        // Reject excessive inputs before allocating a second full vector.
        if(envelopeOnly) {
            if(std::size_t(data->y.size())>filtering::maximumSamples)
                throw std::length_error("Hilbert input exceeds sample budget");
        } else (void)filtering::fftLength(shape,std::size_t(data->y.size()));
        const auto cancel=[&]{return c.cancellation.isCancelled();};
        const auto series=[&](const std::vector<double>& y,const QString& label) {
            return makeData(SeriesData{data->x,QVector<double>(y.begin(),y.end()),data->xAxis,{label,data->yAxis.unit}});
        };
        std::vector<double> values(data->y.begin(),data->y.end());
        QMap<QString,DataRef> outputs;
        QJsonObject report=science_detail::numericBackendReport();
        report["sample_interval"]=dt;report["input_samples"]=double(values.size());report["resampled"]=false;
        report["hilbert_convention"]="N_point_finite_block_DC_and_even_Nyquist_real_only";
        report["amplitude_unit"]=data->yAxis.unit;
        double removedMean=0;
        if(!envelopeOnly) {
            if(c.progress)c.progress(10,"progress.filter");
            auto f=filtering::bandpass(values,dt,shape,low,high,p["rolloff_exponent"].toDouble(12),cancel);
            report["nfft"]=double(f.fftLength);report["frequency_resolution"]=(1/dt)/double(f.fftLength);
            report["lower_frequency"]=low;report["upper_frequency"]=high;
            report["edge_amplitude_gain"]=1/std::sqrt(2.);
            report["filter_shape"]=shape==filtering::Shape::Gaussian?"gaussian":"taner_acycle_corrected_v2";
            report["boundary"]=shape==filtering::Shape::Gaussian?"zero_padded_10_next_power_two_crop_first_N":"periodic_N_point";
            report["reference"]= "Acycle_3.1.1_validation_snapshot_acycleBandpassFilter_corrected-bandpass-v2";
            if(shape==filtering::Shape::Taner)report["rolloff_exponent"]=p["rolloff_exponent"];
            removedMean=f.removedMean;values=std::move(f.filtered);
            outputs.insert("filtered",series(values,"Filtered value"));
            const QString inverseUnit=data->xAxis.unit.isEmpty()?QString():"1/"+data->xAxis.unit;
            outputs.insert("filter_response",makeData(SeriesData{
                QVector<double>(f.frequency.begin(),f.frequency.end()),QVector<double>(f.gain.begin(),f.gain.end()),
                {"Frequency",inverseUnit},{"Amplitude gain","1"}}));
        }
        if(c.progress)c.progress(70,"progress.hilbert");
        const bool demean=envelopeOnly && p["hilbert_remove_mean"].toBool();
        const auto analytic=filtering::hilbert(values,demean,cancel);
        if(envelopeOnly) { removedMean=analytic.removedMean;outputs.insert("quadrature",series(analytic.quadrature,"Hilbert quadrature")); }
        outputs.insert("envelope",series(analytic.envelope,"Amplitude envelope"));
        report["removed_mean"]=removedMean;report["demeaned"]=!envelopeOnly || demean;
        report["hilbert_samples"]=double(values.size());
        if(c.cancellation.isCancelled())return NodeResult::cancelled();
        auto result=NodeResult::success(std::move(outputs),report);
        result.warnings<<"warning.filter_endpoints"<<"warning.hilbert_interpretation";
        return result;
    } catch(const filtering::Cancelled&) { return NodeResult::cancelled(); }
      catch(const std::length_error& e) { return fail("FILTER_BUDGET",QString::fromUtf8(e.what()),{{"maximum_fft",double(filtering::maximumFftLength)},{"maximum_samples",double(filtering::maximumSamples)}}); }
      catch(const std::bad_alloc&) { return NodeResult::failure(makeError(ErrorCode::AllocationFailure)); }
      catch(const std::overflow_error& e) { return NodeResult::failure(makeError(ErrorCode::NumericRange,{},QString::fromUtf8(e.what()))); }
      catch(const std::exception& e) { return fail("FILTER_FAILED",QString::fromUtf8(e.what())); }
}
}
TaskDescriptor GaussianFilterTask::descriptor() { return filterDescriptor("Gaussian band-pass and envelope"); }
TaskDescriptor TanerFilterTask::descriptor() {
    auto d=filterDescriptor("Taner band-pass and envelope");
    auto q=task_detail::number("rolloff_exponent","Roll-off exponent q",12,.001,20);q.group="filter_shape";
    d.parameters.push_back(q);return d;
}
TaskDescriptor HilbertEnvelopeTask::descriptor() {
    auto d=task_detail::seriesTransform("Hilbert amplitude envelope",taskCategory(TaskCategory::Envelope));
    d.outputs={{"envelope","Amplitude envelope",DataKind::Series,true},{"quadrature","Hilbert quadrature",DataKind::Series,true}};
    d.defaultOutput="envelope";d.parameters={science_detail::boolean("hilbert_remove_mean","Remove mean",false)};
    return d;
}
NodeResult GaussianFilterTask::execute(const TaskInput& i,const ExecutionContext& c){return executeFilter(i,c,m_parameters,false,filtering::Shape::Gaussian);}
NodeResult TanerFilterTask::execute(const TaskInput& i,const ExecutionContext& c){return executeFilter(i,c,m_parameters,false,filtering::Shape::Taner);}
NodeResult HilbertEnvelopeTask::execute(const TaskInput& i,const ExecutionContext& c){return executeFilter(i,c,m_parameters,true,filtering::Shape::Gaussian);}
}
