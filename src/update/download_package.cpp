// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "update/download_package.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <archive.h>
#include <archive_entry.h>
#include <cmath>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif
namespace aster::updates {
namespace {
QString entryFor(const QString &platform) {
    if(platform=="windows-x64")return "Aster.exe";
    if(platform=="linux-x64")return "AppRun";
    if(platform=="macos-arm64"||platform=="macos-x86_64")return "Aster.app/Contents/MacOS/Aster";
    return {};
}
bool safePath(const QString &path) {
    if(path.isEmpty()||path.size()>1024||path.contains('\\')||path.contains(':')||path.startsWith('/'))return false;
    static const QRegularExpression forbidden("[\\x00-\\x1f<>\"|?*]");
    static const QRegularExpression device("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption);
    for(const auto &part:path.split('/'))if(part.isEmpty()||part=="."||part==".."||part.endsWith('.')||part.endsWith(' ')||forbidden.match(part).hasMatch()||device.match(part).hasMatch())return false;
    return true;
}
qint64 integer(const QJsonValue &v,qint64 limit) {
    const auto d=v.toDouble(-1);return std::isfinite(d)&&d>=1&&d<=double(limit)&&std::floor(d)==d?qint64(d):0;
}
}
QString nativeUpdatePlatform() {
    const auto cpu=QSysInfo::buildCpuArchitecture();
#if defined(Q_OS_WIN)
    return cpu=="x86_64"?"windows-x64":QString{};
#elif defined(Q_OS_MACOS)
    if(cpu=="arm64")return "macos-arm64";
    if(cpu=="x86_64")return "macos-x86_64";
#elif defined(Q_OS_LINUX)
    if(cpu=="x86_64")return "linux-x64";
#endif
    return {};
}
bool allowedPackageUrl(const QUrl &url,const UpdateConfig &config,const QString &source,bool redirect) {
    if(!url.isValid()||url.scheme()!="https"||!url.userInfo().isEmpty()||url.port(-1)!=-1||url.hasFragment())return false;
    if(source=="GitHub") {
        if(url.host()=="github.com")return url.path().startsWith("/"+config.owner+"/"+config.repository+"/releases/download/");
        return redirect&&(url.host()=="release-assets.githubusercontent.com"||url.host()=="objects.githubusercontent.com");
    }
    if(source!="Gitee")return false;
    if(url.host()=="gitee.com"){
        const auto prefix="/"+config.owner+"/"+config.repository;
        if(url.path().startsWith(prefix+"/releases/download/"))return true;
        return redirect && QRegularExpression("^"+QRegularExpression::escape(prefix)+"/attach_files/[0-9]+/download(?:/[^/]+)?$").match(url.path()).hasMatch();
    }
    // Observed official Gitee attachment CDN. Never accept it as a primary
    // metadata source; only follow HTTPS redirects and verify package SHA-256.
    return redirect && url.host()=="foruda.gitee.com" && url.path().startsWith("/attach_file/");
}
std::optional<DownloadPackage> parseDownloadPackage(const QByteArray &data,const UpdateConfig &config,const QString &version,const QString &platform,QString *error) {
    auto fail=[&](const char *key)->std::optional<DownloadPackage>{if(error)*error=key;return {};};
    if(data.size()>1024*1024)return fail("update.package_invalid");
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(data,&parse);const auto o=doc.object();
    auto v=version;if(v.startsWith('v'))v.remove(0,1);
    const auto parsed=ReleaseVersion::parse(v);
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()||o["schema"].toInt()!=1||o["version"].toString()!=v||!parsed||isAlphaVersion(v)||(!parsed->prerelease.isEmpty()&&parsed->prerelease.first()!="beta")||entryFor(platform).isEmpty()||!o["packages"].isArray())return fail("update.package_invalid");
    std::optional<DownloadPackage> found;
    for(const auto &value:o["packages"].toArray()) {
        const auto item=value.toObject();if(item["platform"].toString()!=platform)continue;
        if(found)return fail("update.package_invalid");
        DownloadPackage p;p.version=v;p.platform=platform;p.root="Aster-"+v+"-"+platform;p.entry=entryFor(platform);
        p.file=item["file"].toString();const auto suffix=platform=="linux-x64"?".tar.gz":".zip";
        p.bytes=integer(item["bytes"],2LL*1024*1024*1024);p.unpackedBytes=integer(item["unpacked_bytes"],4LL*1024*1024*1024);
        const auto hash=item["sha256"].toString();
        if(p.file!=p.root+suffix||!safePath(p.root)||!p.bytes||!p.unpackedBytes||!QRegularExpression("^[a-f0-9]{64}$").match(hash).hasMatch())return fail("update.package_invalid");
        p.sha256=QByteArray::fromHex(hash.toLatin1());QSet<QString> names;
        for(const auto &source:item["sources"].toArray()) {
            const auto s=source.toObject();PackageSource origin{s["name"].toString(),QUrl(s["url"].toString())};
            if(names.contains(origin.name)||!allowedPackageUrl(origin.url,config,origin.name)||origin.url.fileName()!=p.file||origin.url.hasQuery())return fail("update.package_invalid");
            const auto tag=origin.url.path().section('/',-2,-2);
            if(tag!=v&&tag!="v"+v)return fail("update.package_invalid");
            names.insert(origin.name);p.sources.append(origin);
        }
        if(p.sources.isEmpty()||p.sources.size()>2)return fail("update.package_invalid");
        found=p;
    }
    if(!found)return fail("update.package_missing");
    if(error)error->clear();
    return found;
}
ExtractionResult extractDownload(const QString &archivePath,const QString &staging,const DownloadPackage &p,const std::shared_ptr<std::atomic_bool> &cancel) {
    auto failure=[](const char *key){return ExtractionResult{false,false,QString::fromLatin1(key),{}};};
    if(!safePath(p.root)||p.entry!=entryFor(p.platform)||QFileInfo::exists(staging)||!QDir().mkdir(staging))return failure("update.extract_failed");
    std::unique_ptr<archive,decltype(&archive_read_free)> a(archive_read_new(),archive_read_free);
    archive_read_support_filter_gzip(a.get());archive_read_support_format_tar(a.get());archive_read_support_format_zip(a.get());
    QFile input(archivePath);if(!input.open(QIODevice::ReadOnly))return failure("update.extract_failed");
    // QFile handles Unicode paths on Windows; libarchive consumes its descriptor.
    if(archive_read_open_fd(a.get(),input.handle(),65536)!=ARCHIVE_OK)return failure("update.extract_failed");
    struct Link {QString name,target;};QVector<Link> links;
    QSet<QString> seen;QSet<QString> parents;qint64 total=0;int count=0;archive_entry *entry=nullptr;int code;
    const QString packageRoot=QDir(staging).filePath(p.root);
    while((code=archive_read_next_header(a.get(),&entry))==ARCHIVE_OK) {
        if(cancel->load())return {false,true,{}, {}};
        if(++count>50000)return failure("update.extract_limit");
        const auto *raw=archive_entry_pathname_utf8(entry);if(!raw)raw=archive_entry_pathname(entry);
        QString name=QString::fromUtf8(raw?raw:"");while(name.endsWith('/'))name.chop(1);
        if(!safePath(name))return failure("update.archive_unsafe");
        // ditto's resource-fork sidecars do not belong to the portable app tree.
        if(name=="__MACOSX"||name.startsWith("__MACOSX/")){
            if(!p.platform.startsWith("macos"))return failure("update.archive_unsafe");
            const auto size=archive_entry_size(entry);
            if(size<0||size>p.unpackedBytes-total)return failure("update.extract_limit");
            char discard[65536];la_ssize_t n; qint64 consumed=0;
            while((n=archive_read_data(a.get(),discard,sizeof discard))>0){
                if(cancel->load())return {false,true,{}, {}};
                if(n>size-consumed||n>p.unpackedBytes-total)return failure("update.extract_limit");
                consumed+=n;total+=n;
            }
            if(n<0||consumed!=size)return failure("update.extract_failed");
            continue;
        }
        if(name!=p.root&&!name.startsWith(p.root+'/'))return failure("update.archive_unsafe");
        const auto key=name.normalized(QString::NormalizationForm_C).toCaseFolded();
        if(seen.contains(key)||archive_entry_hardlink(entry)||archive_entry_is_encrypted(entry)>0)return failure("update.archive_unsafe");
        seen.insert(key);const auto type=archive_entry_filetype(entry);
        if(type!=AE_IFREG&&type!=AE_IFDIR&&type!=AE_IFLNK)return failure("update.archive_unsafe");
        if(type!=AE_IFDIR&&parents.contains(key))return failure("update.archive_unsafe");
        auto parent=name.section('/',0,-2);while(parent!=name&&!parent.isEmpty()) {parents.insert(parent.normalized(QString::NormalizationForm_C).toCaseFolded());const auto next=parent.section('/',0,-2);if(next==parent)break;parent=next;}
        // Symlinks are deferred: extraction never writes through a link.
        const auto path=QDir(staging).filePath(name);
        if(!QDir().mkpath(QFileInfo(path).absolutePath()))return failure("update.disk_error");
        if(type==AE_IFDIR){if(!QDir().mkpath(path))return failure("update.disk_error");continue;}
        if(type==AE_IFLNK) {
#ifdef Q_OS_WIN
            return failure("update.archive_unsafe");
#else
            const auto *target=archive_entry_symlink_utf8(entry);if(!target)target=archive_entry_symlink(entry);
            QString link=QString::fromUtf8(target?target:"");
            if(link.isEmpty()||link.size()>1024||link.startsWith('/')||link.contains('\\')||link.contains(':')||link.contains(QChar::Null))return failure("update.archive_unsafe");
            const auto resolved=QDir::cleanPath(name.section('/',0,-2)+'/'+link);
            if(resolved!=p.root&&!resolved.startsWith(p.root+'/'))return failure("update.archive_unsafe");
            links.append({name,link});continue;
#endif
        }
        const auto size=archive_entry_size(entry);
        if(size<0||size>p.unpackedBytes-total)return failure("update.extract_limit");
        QFile output(path);if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return failure("update.archive_unsafe");
        char buffer[65536];qint64 written=0;la_ssize_t n;
        while((n=archive_read_data(a.get(),buffer,sizeof buffer))>0) {
            if(cancel->load())return {false,true,{}, {}};
            if(n>size-written||n>p.unpackedBytes-total)return failure("update.extract_limit");
            if(output.write(buffer,n)!=n)return failure("update.disk_error");
            written+=n;total+=n;
        }
        if(n<0||written!=size||!output.flush())return failure("update.extract_failed");
        output.close();
#ifndef Q_OS_WIN
        auto permissions=QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ReadGroup|QFileDevice::ReadOther;
        if(archive_entry_perm(entry)&0111)permissions|=QFileDevice::ExeOwner|QFileDevice::ExeGroup|QFileDevice::ExeOther;
        if(!QFile::setPermissions(path,permissions))return failure("update.disk_error");
#endif
    }
    if(code!=ARCHIVE_EOF)return failure("update.extract_failed");
#ifndef Q_OS_WIN
    for(const auto &link:links) {
        if(cancel->load())return {false,true,{}, {}};
        if(::symlink(QFile::encodeName(link.target).constData(),QFile::encodeName(QDir(staging).filePath(link.name)).constData())!=0)return failure("update.archive_unsafe");
    }
    for(const auto &link:links){const QFileInfo info(QDir(staging).filePath(link.name));const auto path=info.canonicalFilePath();if(!info.exists()||!path.startsWith(packageRoot+'/'))return failure("update.archive_unsafe");}
#endif
    QFile marker(QDir(packageRoot).filePath("aster-package.json"));
    if(!marker.open(QIODevice::ReadOnly)||marker.size()>4096)return failure("update.package_mismatch");
    const auto info=QJsonDocument::fromJson(marker.readAll()).object();
    const QFileInfo executable(QDir(packageRoot).filePath(p.entry));
    if(info["schema"].toInt()!=1||info["version"].toString()!=p.version||info["platform"].toString()!=p.platform||!executable.isFile()||executable.isSymLink()||executable.size()==0)return failure("update.package_mismatch");
#ifndef Q_OS_WIN
    if(!executable.isExecutable())return failure("update.package_mismatch");
#endif
    return {true,false,{},packageRoot};
}
}
