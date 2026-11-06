// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/services/ar1_reference_cache.h"
#include "math/backend/numeric_backend.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <cmath>
#include <algorithm>

namespace aster {
namespace {
constexpr quint32 magic=0x41473101; // AG1, format/statistic v1
void stop(const std::function<bool()>& cancelled){if(cancelled&&cancelled())throw std::runtime_error("Cancellation requested");}
QByteArray settings(std::size_t n,const numeric::Ar1GlobalOptions& o){
    const auto b=numeric::backend::backendInfo();
    return QJsonDocument(QJsonObject{{"method",1},{"n",double(n)},{"analysis",o.analysisReplicates},{"screen",o.screeningReplicates},
        {"seed",QString::number(o.seed)},{"lowess",o.lowess},{"fraction",o.lowessFraction},{"lower",o.rhoLower},{"upper",o.rhoUpper},
        {"beta",o.screeningBudget},{"variation",o.variationBudget},{"nw",2},{"k",3},{"fft_length",double(n)},
        {"rng","SplitMix64-BoxMuller-v1"},{"abi",QSysInfo::buildAbi()},{"linear",QString::fromStdString(b.linearAlgebra)},
        {"fft",QString::fromStdString(b.fft)}}).toJson(QJsonDocument::Compact);
}
std::shared_ptr<const numeric::Ar1ReferenceBank> readBank(const QString& path,const QByteArray& key,std::size_t n,
    const numeric::Ar1GlobalOptions& o,std::size_t cells,const std::function<bool()>& cancelled){
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};
    const qint64 rowBytes=qint64(o.screeningReplicates+o.analysisReplicates)*8;
    if(f.size()!=12+key.size()+qint64(cells)*rowBytes+32)return {};
    QDataStream in(&f);in.setVersion(QDataStream::Qt_6_0);quint32 tag=0,length=0,count=0;
    in>>tag>>length;if(tag!=magic||length!=quint32(key.size()))return {};
    QByteArray storedKey(int(length),'\0');if(in.readRawData(storedKey.data(),storedKey.size())!=storedKey.size()||storedKey!=key)return {};
    in>>count;if(count!=cells)return {};
    auto bank=std::make_shared<numeric::Ar1ReferenceBank>();bank->samples=n;bank->options=o;bank->rows.reserve(cells);
    QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(key);
    for(std::size_t i=0;i<cells;++i){
        stop(cancelled);QByteArray bytes(int(rowBytes),'\0');if(in.readRawData(bytes.data(),bytes.size())!=bytes.size())return {};
        hash.addData(bytes);QDataStream row(bytes);row.setVersion(QDataStream::Qt_6_0);
        numeric::Ar1ReferenceRow values;values.screening.resize(o.screeningReplicates);values.analysis.resize(o.analysisReplicates);
        for(auto* a:{&values.screening,&values.analysis}){
            for(auto& v:*a){row>>v;if(!std::isfinite(v))return {};}
            if(!std::is_sorted(a->begin(),a->end()))return {};
        }
        if(row.status()!=QDataStream::Ok)return {};
        bank->rows.push_back(std::move(values));
    }
    QByteArray checksum(32,'\0');if(in.readRawData(checksum.data(),32)!=32||checksum!=hash.result()||!f.atEnd())return {};
    return bank;
}
bool writeBank(const QString& path,const QByteArray& key,const numeric::Ar1ReferenceBank& bank,const std::function<bool()>& cancelled){
    QSaveFile f(path);if(!f.open(QIODevice::WriteOnly))return false;
    QDataStream out(&f);out.setVersion(QDataStream::Qt_6_0);out<<magic<<quint32(key.size());out.writeRawData(key.constData(),key.size());out<<quint32(bank.rows.size());
    QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(key);
    for(const auto& row:bank.rows){
        stop(cancelled);QByteArray bytes;QDataStream values(&bytes,QIODevice::WriteOnly);values.setVersion(QDataStream::Qt_6_0);
        for(const auto* v:{&row.screening,&row.analysis})for(double d:*v)values<<d;
        hash.addData(bytes);out.writeRawData(bytes.constData(),bytes.size());
    }
    const auto digest=hash.result();out.writeRawData(digest.constData(),digest.size());stop(cancelled);
    return out.status()==QDataStream::Ok && f.commit();
}
void prune(const QString& directory,const QString& keep){
    const auto files=QDir(directory).entryInfoList({"*.bank"},QDir::Files,QDir::Time);
    qint64 total=0;int count=0;for(const auto& f:files){total+=f.size();++count;}
    for(auto i=files.crbegin();i!=files.crend()&&(total>384LL*1024*1024||count>4);++i){
        if(i->absoluteFilePath()==keep)continue;
        QLockFile lock(i->absoluteFilePath()+".lock");lock.setStaleLockTime(0);
        if(lock.tryLock(0)&&QFile::remove(i->absoluteFilePath())){total-=i->size();--count;}
    }
}
}
Ar1ReferenceCache::Ar1ReferenceCache(QString directory):m_directory(std::move(directory)){}
QString Ar1ReferenceCache::defaultDirectory(){const auto base=QStandardPaths::writableLocation(QStandardPaths::CacheLocation);return base.isEmpty()?QString():base+"/ar1_global/v1";}
Ar1ReferenceAccess Ar1ReferenceCache::acquire(std::size_t n,const numeric::Ar1GlobalOptions& o,
    const std::function<bool()>& cancelled,const std::function<void(int,const QString&)>& progress)const{
    const auto grid=numeric::ar1GlobalGrid(n,o);stop(cancelled);
    const auto key=settings(n,o);Ar1ReferenceAccess result;
    result.key=QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex());
    QString path;std::unique_ptr<QLockFile> lock;
    if(!m_directory.isEmpty()&&QDir().mkpath(m_directory)){
        path=QDir(m_directory).absoluteFilePath(result.key+".bank");
        lock=std::make_unique<QLockFile>(path+".lock");lock->setStaleLockTime(0);
        bool reported=false;
        while(!lock->tryLock(0)){
            stop(cancelled);
            if(lock->error()!=QLockFile::LockFailedError){lock.reset();path.clear();break;}
            if(progress&&!reported){progress(0,"progress.ar1_waiting");reported=true;}
            QThread::msleep(50);
        }
        stop(cancelled);
        if(!path.isEmpty()){
            if(progress)progress(1,"progress.ar1_loading");
            result.bank=readBank(path,key,n,o,grid.size(),cancelled);
            if(result.bank){result.hit=true;result.saved=true;return result;}
        }
    }
    if(!m_directory.isEmpty()&&path.isEmpty())result.warning="ar1_cache_unavailable";
    result.bank=numeric::buildAr1ReferenceBank(n,o,cancelled,[&](int percent){if(progress)progress(percent*95/100,"progress.ar1_generating");});
    if(!path.isEmpty()){
        result.saved=writeBank(path,key,*result.bank,cancelled);
        if(!result.saved)result.warning="ar1_cache_write_failed";
        else prune(m_directory,path);
    }
    return result;
}
}
