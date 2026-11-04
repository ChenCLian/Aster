#include "processing/task_categories.h"
#include "processing/ar1_global_task.h"
#include "processing/services/ar1_reference_cache.h"
#include "processing/science_helpers.h"
#include "processing/task_helpers.h"
namespace aster {
TaskDescriptor Ar1GlobalTask::descriptor(){
    TaskDescriptor d;d.label="AR1 background global test";d.category=taskCategory(TaskCategory::Significance);d.inputKind=DataKind::Series;d.defaultOutput="ar1_global_test";
    d.outputs={{"ar1_global_test","Global test result",DataKind::Table,true},
        {"spectrum","Power spectrum",DataKind::Spectrum,true},{"ar1_global_background","Fitted AR1 background",DataKind::Spectrum,true},
        {"ar1_analysed_series","Analysed series",DataKind::Series,true}};
    d.parameters={science_detail::text("ar1_precision","Simulation precision","standard",{"standard","fine"}),
        task_detail::number("ar1_seed","Reference simulation seed",202611021,0,2147483647,ParameterKind::Integer),
        science_detail::boolean("ar1_lowess","Replay internal LOWESS",false),
        task_detail::number("ar1_fraction","Internal LOWESS window fraction",.35,.05,1),
        science_detail::boolean("ar1_cache","Reuse matching reference simulations",true)};
    return d;
}
Ar1GlobalTask::Ar1GlobalTask(const QJsonObject& p){
    m_cacheDirectory=Ar1ReferenceCache::defaultDirectory();
    m_options.analysisReplicates=p["ar1_precision"].toString()=="fine"?999:199;
    m_options.seed=std::uint64_t(p["ar1_seed"].toInt());m_options.lowess=p["ar1_lowess"].toBool();
    m_options.lowessFraction=m_options.lowess?p["ar1_fraction"].toDouble():.35;m_cache=p["ar1_cache"].toBool();
}
Ar1GlobalTask::Ar1GlobalTask(const QJsonObject& p,QString directory):Ar1GlobalTask(p){m_cacheDirectory=std::move(directory);}
NodeResult Ar1GlobalTask::execute(const TaskInput& input,const ExecutionContext& context){
    const auto* source=dataAs<SeriesData>(input.data);
    if(!source)return NodeResult::failure(makeError(ErrorCode::InputType,{{"expected","Series"}}));
    const auto invalid=validateData(input.data);if(!invalid.isEmpty())return NodeResult::failure(makeError(ErrorCode::InputInvalid,{},invalid));
    double dt=0;const auto error=science_detail::uniformSpacing(*source,dt);if(!error.isEmpty())return NodeResult::failure(error);
    const auto cancelled=[&]{return context.cancellation.isCancelled();};
    if(cancelled())return NodeResult::cancelled();
    try{
        const std::vector<double> y(source->y.begin(),source->y.end());
        numeric::ar1GlobalGrid(y.size(),m_options);
        // Fail invalid/constant/degenerate inputs before spending time on a bank.
        numeric::ar1GlobalStatistic(y,1/dt,m_options,{},cancelled);
        Ar1ReferenceCache cache(m_cache?m_cacheDirectory:QString());
        const auto reference=cache.acquire(y.size(),m_options,cancelled,context.progress);
        if(context.progress)context.progress(97,"progress.ar1_evaluating");
        const auto value=numeric::evaluateAr1Global(y,1/dt,*reference.bank,cancelled);
        const bool available=!value.test.emptyConfidenceSet,rejected=available&&value.test.p<=.05;
        auto r=NodeResult::success({},{{"statistical_method","AR1 composite-null maximum test v1"},
            {"global_p",value.test.p},{"alpha",.05},{"global_test_available",available},{"reject_global_null",rejected},
            {"samples",double(y.size())},{"sample_spacing",dt},{"nw",2},{"tapers",3},{"fft_length",double(y.size())},
            {"rho_domain_lower",m_options.rhoLower},{"rho_domain_upper",m_options.rhoUpper},
            {"screening_budget",m_options.screeningBudget},{"grid_variation_bound",value.variationBound},
            {"analysis_replicates",m_options.analysisReplicates},{"screening_replicates",m_options.screeningReplicates},
            {"reference_seed",QString::number(m_options.seed)},{"reference_key",reference.key},{"reference_cache_hit",reference.hit},
            {"reference_cache_saved",reference.saved},{"reference_cache_note",reference.warning},
            {"candidate_model_count",double(value.test.includedModels.size())},{"reference_model_count",double(reference.bank->rows.size())},
            {"observed_maximum_ratio",value.maximumRatio},{"raw_lag_one",value.lag},{"fitted_rho",value.fit.rho},
            {"fitted_mean",value.fit.mean},{"innovation_variance",value.fit.innovationVariance},
            {"internal_lowess",m_options.lowess},{"lowess_fraction",m_options.lowessFraction},{"lowess_robust_iterations",0},
            {"inference_scope","One incoming record, all positive interior Fourier bins; global AR1-null test, not FDR or individual peak probability"},
            {"preprocessing_scope","Incoming series is the model input. Only the optional internal LOWESS is replayed. Ancestor workflow operations are not replayed; no preprocessing is inserted."},
            {"null_model","Stationary Gaussian AR1, unknown constant mean and positive marginal scale; predetermined rho domain [-.999,.999]"},
            {"reference_scope","Independent reference randomness and observation; fixed-bank conditional rates may vary. Not across records or repeated selection."},
            {"validation_reference","docs/methods/ar1_global.md"},{"numerical_backend",science_detail::numericBackendReport()}});
        auto spectrum=[&](const QString& key,const std::vector<double>& values){
            auto s=science_detail::spectrum(QVector<double>(value.spectrum.frequency.begin(),value.spectrum.frequency.end()),
                QVector<double>(values.begin(),values.end()),source->xAxis.unit);
            if(!source->yAxis.unit.isEmpty()&&!source->xAxis.unit.isEmpty())s.powerAxis.unit="("+source->yAxis.unit+")^2 * ("+source->xAxis.unit+")";
            r.outputs.insert(key,makeData(std::move(s)));
        };
        spectrum("spectrum",value.spectrum.power);spectrum("ar1_global_background",value.background);
        SeriesData series=*source;series.y=QVector<double>(value.analysedSeries.begin(),value.analysedSeries.end());
        r.outputs.insert("ar1_analysed_series",makeData(std::move(series)));
        r.outputs.insert("ar1_global_test",makeData(TableData{{"global_p","alpha","test_available","reject_global_null","fitted_rho","candidate_models","analysis_simulations"},
            {{value.test.p,.05,available?1.:0.,rejected?1.:0.,value.fit.rho,double(value.test.includedModels.size()),double(m_options.analysisReplicates)}}}));
        r.warnings<<"warning.ar1_global_scope";
        if(!available)r.warnings<<"warning.ar1_empty_set";
        if(!reference.warning.isEmpty())r.warnings<<"warning.ar1_cache";
        if(cancelled())return NodeResult::cancelled();
        if(context.progress)context.progress(100,"progress.ar1_complete");
        return r;
    }catch(const std::exception& e){
        return cancelled()?NodeResult::cancelled():NodeResult::failure(makeError(ErrorCode::Ar1GlobalFailure,
            {{"samples",double(source->y.size())},{"simulations",m_options.analysisReplicates}},QString::fromUtf8(e.what())));
    }
}
}
