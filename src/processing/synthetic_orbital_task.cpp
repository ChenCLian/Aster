// SPDX-License-Identifier: GPL-3.0-or-later
#include "synthetic_orbital_task.h"
#include "processing/task_helpers.h"
#include "processing/science_helpers.h"
#include "processing/task_categories.h"
#include "math/synthetic/orbital_proxy.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <new>
#include <stdexcept>
static void initializeSyntheticResources() { Q_INIT_RESOURCE(synthetic); }
namespace aster {
namespace {
const QJsonObject& configuration() {
    static const QJsonObject value=[] {
        initializeSyntheticResources();QFile f(":/synthetic/model.json");
        if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Synthetic model resource missing");
        QJsonParseError error;const auto object=QJsonDocument::fromJson(f.readAll(),&error).object();
        if(error.error!=QJsonParseError::NoError||object["model_version"].toInt()!=1)throw std::runtime_error("Invalid synthetic model resource");
        return object;
    }();return value;
}
std::vector<double> templateDepth() {
    static const std::vector<double> data=[] {
        const auto c=configuration();QFile f(":/synthetic/depth_m.txt");
        if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Depth template resource missing");
        const auto bytes=f.readAll();
        if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=c["template_sha256"].toString().toLatin1())throw std::runtime_error("Depth template checksum mismatch");
        std::vector<double> x;for(const auto& line:bytes.split('\n'))if(!line.trimmed().isEmpty()){
            bool ok=false;double v=line.toDouble(&ok);if(!ok)throw std::runtime_error("Invalid template depth");x.push_back(v);
        }
        if(x.size()!=std::size_t(c["template_samples"].toInt()))throw std::runtime_error("Depth template count mismatch");
        return x;
    }();return data;
}
QVector<double> qvector(const std::vector<double>& v) {return QVector<double>(v.begin(),v.end());}
template<std::size_t N> QJsonArray array(const std::array<double,N>& a) {QJsonArray v;for(double x:a)v.append(x);return v;}
}
TaskDescriptor SyntheticOrbitalTask::descriptor() {
    auto d=task_detail::seriesTransform("Synthetic orbital proxy",taskCategory(TaskCategory::TestSignals));d.inputKind.reset();
    const auto defaults=configuration()["defaults"].toObject();
    auto num=[&](const char* key,const char* label,double lo,double hi,bool integer=false){return task_detail::number(key,label,defaults[key].toDouble(),lo,hi,integer?ParameterKind::Integer:ParameterKind::Number);};
    d.parameters={science_detail::text("synthetic_grid","Depth grid",defaults["synthetic_grid"].toString(),{"odp1260","uniform"}),
        num("synthetic_start_m","Start depth (m)",-10000,10000),num("synthetic_span_m","Thickness (m)",.001,10000),num("synthetic_count","Samples",32,100000,true),
        num("synthetic_seed","Random seed",0,2147483647,true),num("synthetic_realization","Realization index",0,2147483647,true),
        num("synthetic_rate_min","Minimum sedimentation rate (cm/kyr)",.01,1000),num("synthetic_rate_max","Maximum sedimentation rate (cm/kyr)",.01,1000),
        num("synthetic_snr","Signal/noise variance ratio",.01,10000),num("synthetic_rho","AR1 correlation per sample",-.95,.95)};
    for(auto& p:d.parameters){p.group="synthetic_model";if(p.key=="synthetic_start_m"||p.key=="synthetic_span_m"||p.key=="synthetic_count")p.visibleWhen={{"synthetic_grid","uniform"}};}
    d.outputs={{"series","Noisy proxy vs depth",DataKind::Series,true},{"clean_signal","Clean signal",DataKind::Series,true},
        {"scaled_noise","Scaled AR1 noise",DataKind::Series,true},{"age_model","True relative age model",DataKind::Series,true},
        {"sedimentation_rate","True sedimentation rate",DataKind::Series,true},{"time_series","Noisy proxy vs relative age",DataKind::Series,true},
        {"harmonics","True harmonic parameters",DataKind::Table,true}};
    return d;
}
NodeResult SyntheticOrbitalTask::execute(const TaskInput&,const ExecutionContext& context) {
    if(context.cancellation.isCancelled())return NodeResult::cancelled();
    try {
        // Normalize through the same descriptor as the UI, including direct API calls.
        TaskRegistry registry;auto d=descriptor();d.key="synthetic_orbital";
        registry.add(d,[](const QJsonObject& p){return std::make_unique<SyntheticOrbitalTask>(p);});
        const auto normalized=registry.normalize(d.key,m_parameters);
        if(!normalized.validation.ok())return NodeResult::failure(makeError(ErrorCode::ParameterInvalid,{},normalized.validation.message()));
        const auto p=normalized.parameters,c=configuration();const auto smooth=c["smooth_logit"].toObject();
        synthetic::Model m{};
        auto means=c["frequency_mean_arcsec_yr"].toArray(),sds=c["frequency_sd_arcsec_yr"].toArray();
        if(means.size()!=5||sds.size()!=5)throw std::runtime_error("Invalid frequency model resource");
        for(int i=0;i<5;++i){m.frequencyMean[i]=means[i].toDouble(qQNaN());m.frequencySd[i]=sds[i].toDouble(qQNaN());}
        m.g5=c["g5"].toDouble(qQNaN());m.interceptSd=smooth["intercept_sd"].toDouble(qQNaN());m.slopeSd=smooth["slope_sd"].toDouble(qQNaN());m.residualSd=smooth["residual_sd"].toDouble(qQNaN());m.lengthScale=smooth["length_scale"].toDouble(qQNaN());
        std::vector<double> depth;
        const bool builtin=p["synthetic_grid"].toString()=="odp1260";
        if(builtin)depth=templateDepth();else {
            const int n=p["synthetic_count"].toInt();const double start=p["synthetic_start_m"].toDouble(),span=p["synthetic_span_m"].toDouble();
            depth.resize(std::size_t(n));for(int i=0;i<n;++i)depth[std::size_t(i)]=start+span*(double(i)/(n-1));
        }
        synthetic::Options options{p["synthetic_rate_min"].toDouble(),p["synthetic_rate_max"].toDouble(),p["synthetic_snr"].toDouble(),p["synthetic_rho"].toDouble()};
        const auto latent=synthetic::draw(m,depth.size(),std::uint64_t(p["synthetic_seed"].toInt()),std::uint64_t(p["synthetic_realization"].toInt()));
        const auto r=synthetic::evaluate(m,options,depth,latent,[&]{return context.cancellation.isCancelled();});
        if(r.cancelled)return NodeResult::cancelled();
        const auto x=qvector(r.depth),age=qvector(r.ageKa);
        auto curve=[&](const std::vector<double>& y,const char* name,const char* unit){return makeData(SeriesData{x,qvector(y),{"Depth",builtin?"rmcd":"m"},{name,unit}});};
        TableData table;table.columns={"Frequency (1/ka)","Period (ka)","Sine coefficient","Cosine coefficient"};
        constexpr double pi=3.141592653589793238462643383279502884;
        for(int i=0;i<10;++i){double freq=r.angularFrequency[i]/(2*pi*1000);table.rows.push_back({freq,1/freq,latent.beta[i],latent.beta[i+10]});}
        QJsonObject report{{"model","synthetic_orbital_proxy"},{"model_version",1},{"seed",p["synthetic_seed"]},{"realization",p["synthetic_realization"]},
            {"rng","SplitMix64_BoxMuller_v1_four_streams_not_NumPy_seed_compatible"},{"sampling",builtin?"PANGAEA_817206_depth_template_irregular":"uniform_depth"},
            {"samples",double(depth.size())},{"snr_clean_over_noise_variance",options.snr},{"rho_per_sample",options.rho},{"ar1_burnin",256},
            {"frequency_arcsec_yr",array(latent.frequency)},{"g5_arcsec_yr",m.g5},{"beta_sin_then_cos",array(latent.beta)},{"inverse_rate_knots_Myr_per_m",array(r.inverseKnots)},
            {"clipped_grid_fraction",r.clippedFraction},{"duration_ka",r.ageKa.back()},{"relative_age_zero","first_depth_no_absolute_epoch"},
            {"interpolation","not_a_knot_cubic_inverse_rate_then_clip"},{"integration","cumulative_trapezoid_on_actual_depth"},
            {"normalization","population_sd_ddof0; proxy=(clean+noise-raw_mean)/raw_sd"},{"raw_mean",r.rawMean},{"raw_sd",r.rawSd},
            {"source_license","GPL-3.0-or-later; AstroGeoFit (C) 2025 CNRS, J. Laskar"}};
        if(builtin){report["template_source"]="https://doi.org/10.1594/PANGAEA.817206";report["template_license"]="CC-BY-3.0";report["template_sha256"]=c["template_sha256"];}
        auto result=NodeResult::success({{"series",curve(r.proxy,"Synthetic proxy","1")},{"clean_signal",curve(r.clean,"Clean signal","1")},
            {"scaled_noise",curve(r.noise,"Scaled AR1 noise","1")},{"age_model",curve(r.ageKa,"Relative age","ka")},
            {"sedimentation_rate",curve(r.rate,"Sedimentation rate","cm/kyr")},
            {"time_series",makeData(SeriesData{age,qvector(r.proxy),{"Relative age","ka"},{"Synthetic proxy","1"}})},{"harmonics",makeData(std::move(table))}},{{"synthetic_source",report}});
        result.warnings<<"warning.synthetic_model";
        if(builtin)result.warnings<<"warning.synthetic_irregular";
        const double shortestPeriodKa=2*pi*1000/(*std::max_element(r.angularFrequency.begin(),r.angularFrequency.end()));
        double maxGap=0;for(std::size_t i=1;i<r.ageKa.size();++i)maxGap=std::max(maxGap,r.ageKa[i]-r.ageKa[i-1]);
        if(maxGap>=shortestPeriodKa/2)result.warnings<<"warning.synthetic_undersampled";
        if(context.cancellation.isCancelled())return NodeResult::cancelled();
        return result;
    }catch(const std::bad_alloc&){return NodeResult::failure(makeError(ErrorCode::AllocationFailure));}
    catch(const std::exception& e){return NodeResult::failure(ErrorInfo{"SYNTHETIC_ORBITAL",{},QString::fromUtf8(e.what())});}
}
}
