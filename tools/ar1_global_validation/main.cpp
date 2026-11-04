// Focused validation of the exact production implementation, not a second algorithm.
#include "math/statistics/ar1_global.h"
#include "processing/services/ar1_reference_cache.h"
#include "portable_random.h"
#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QTextStream>
#include <cmath>
using namespace aster;
int main(int argc,char** argv){QCoreApplication app(argc,argv);
 try{
    if(argc!=4)throw std::runtime_error("Usage: ar1_global_validation cases.json cache_directory result.json");
    QFile f(QString::fromLocal8Bit(argv[1]));if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read cases");
    QJsonParseError error;const auto c=QJsonDocument::fromJson(f.readAll(),&error).object();
    if(error.error!=QJsonParseError::NoError)throw std::runtime_error("Invalid JSON");
    const auto n=c["n"].toInt();numeric::Ar1GlobalOptions o;o.seed=c["bank_seed"].toVariant().toULongLong();
    o.analysisReplicates=c["replicates"].toInt();o.lowess=c["lowess"].toBool();
    QElapsedTimer timer;timer.start();int last=-1;Ar1ReferenceCache cache(QString::fromLocal8Bit(argv[2]));
    const auto reference=cache.acquire(n,o,{},[&](int p,const QString& phase){if(p/10!=last){last=p/10;QTextStream(stdout)<<phase<<" "<<p<<Qt::endl;}});
    const qint64 bankMs=timer.elapsed();QJsonArray cases;
    for(const auto& v:c["cases"].toArray()){
        const auto item=v.toObject();const double rho=item["rho"].toDouble(),amplitude=item["amplitude"].toDouble(),period=item["period"].toDouble(16);
        const int trials=item["trials"].toInt();if(trials<1||trials>10000||std::abs(rho)>.999||period<2)throw std::runtime_error("Invalid scenario");
        CalibrationRandom random(item["seed"].toVariant().toULongLong());int hits=0,empty=0;double sumP=0;
        timer.restart();
        for(int k=0;k<trials;++k){auto y=random.ar1(n,rho);for(int j=0;j<n;++j)y[j]+=amplitude*std::sin(2*std::acos(-1.)*j/period);
            const auto r=numeric::evaluateAr1Global(y,1,*reference.bank);hits+=r.test.p<=.05;empty+=r.test.emptyConfidenceSet;sumP+=r.test.p;}
        const double rate=double(hits)/trials,z=1.959963984540054,den=1+z*z/trials;
        const double a=(rate+z*z/(2*trials))/den,b=z*std::sqrt(rate*(1-rate)/trials+z*z/(4.*trials*trials))/den;
        auto row=item;row["hits"]=hits;row["rate"]=rate;row["conditional_bank_wilson95"]=QJsonArray{std::max(0.,a-b),std::min(1.,a+b)};
        row["empty_sets"]=empty;row["failures"]=0;row["mean_p"]=sumP/trials;row["milliseconds"]=double(timer.elapsed());cases.append(row);
        QTextStream(stdout)<<item["name"].toString()<<" "<<hits<<"/"<<trials<<Qt::endl;
    }
    auto result=c;result["cases"]=cases;result["bank_key"]=reference.key;result["bank_cache_hit"]=reference.hit;result["bank_milliseconds"]=double(bankMs);
    result["scope"]="Production AR1 global v1; paired observations across banks. Wilson intervals conditional on realised bank; empty sets retained.";
    QSaveFile out(QString::fromLocal8Bit(argv[3]));if(!out.open(QIODevice::WriteOnly)||out.write(QJsonDocument(result).toJson())<0||!out.commit())throw std::runtime_error("Cannot save result");
    return 0;
 }catch(const std::exception& e){QTextStream(stderr)<<e.what()<<Qt::endl;return 1;}
}
