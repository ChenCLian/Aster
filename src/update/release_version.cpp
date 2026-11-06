// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "update/release_version.h"
#include <QRegularExpression>
namespace aster::updates {
namespace {
bool numeric(const QString &s) {
    for (auto c:s) if (c<'0' || c>'9') return false;
    return !s.isEmpty();
}
int numberCompare(const QString &a,const QString &b) {
    if(a.size()!=b.size()) return a.size()<b.size() ? -1:1;
    return QString::compare(a,b,Qt::CaseSensitive);
}
}
std::optional<ReleaseVersion> ReleaseVersion::parse(QString text) {
    if(text.size()>128) return {};
    if(text.startsWith('v')) text.remove(0,1);
    static const QRegularExpression rx(QStringLiteral(
        "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?(?:\\+([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$"));
    const auto m=rx.match(text); if(!m.hasMatch()||m.capturedLength()!=text.size())return {};
    ReleaseVersion v{m.captured(1),m.captured(2),m.captured(3),{}};
    if(!m.captured(4).isEmpty())v.prerelease=m.captured(4).split('.');
    for(const auto &part:v.prerelease)if(numeric(part)&&part.size()>1&&part.startsWith('0'))return {};
    return v;
}
int ReleaseVersion::compare(const ReleaseVersion &o) const {
    for(const auto &pair:{qMakePair(major,o.major),qMakePair(minor,o.minor),qMakePair(patch,o.patch)}) {
        const int c=numberCompare(pair.first,pair.second);if(c)return c;
    }
    if(prerelease.isEmpty()!=o.prerelease.isEmpty())return prerelease.isEmpty()?1:-1;
    const auto n=qMin(prerelease.size(),o.prerelease.size());
    for(qsizetype i=0;i<n;++i) {
        const bool a=numeric(prerelease[i]),b=numeric(o.prerelease[i]);
        if(a!=b)return a?-1:1;
        const int c=a?numberCompare(prerelease[i],o.prerelease[i]):QString::compare(prerelease[i],o.prerelease[i],Qt::CaseSensitive);
        if(c)return c;
    }
    return prerelease.size()==o.prerelease.size()?0:prerelease.size()<o.prerelease.size()?-1:1;
}
}
