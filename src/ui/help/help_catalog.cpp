// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/help/help_catalog.h"
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
namespace aster::ui {
namespace {
QString read(const QString &path) {
    if (path.startsWith('/') || path.contains("..") || path.contains(':') || path.contains('\\')) return {};
    QFile file(":/help/" + path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}
QString caption(QString title) {
    title.replace(QRegularExpression("\\[([^\\]]+)\\]\\([^)]+\\)"), "\\1");
    title.remove(QRegularExpression("[`*]"));
    return title.trimmed();
}
}
QVector<HelpSection> helpSections(const QString &path) {
    static QHash<QString,QVector<HelpSection>> cache;
    if (cache.contains(path)) return cache.value(path);
    const auto markdown=read(path);
    QVector<HelpSection> sections;
    if (markdown.isEmpty()) return sections;
    if (!path.endsWith(".md",Qt::CaseInsensitive)) {
        sections.append({path.section('/',-1),markdown,{},1});
        cache.insert(path,sections); return sections;
    }
    const QRegularExpression explicitId("<a\\s+(?:id|name)=\"([^\"]+)\"\\s*></a>");
    const QRegularExpression heading("^ {0,3}(#{1,6})\\s+(.+?)\\s*#*\\s*$");
    const QRegularExpression punctuation("[^\\p{L}\\p{N}_\\-\\s]");
    QStringList pending; QString fence, intro;
    QHash<QString,int> occurrences;
    for (const auto &line:markdown.split('\n')) {
        const auto trimmed=line.trimmed();
        bool fenced=!fence.isEmpty();
        if (trimmed.startsWith("```") || trimmed.startsWith("~~~")) {
            const auto marker=trimmed.left(3);
            if (fence.isEmpty()) fence=marker; else if (fence==marker) fence.clear();
            fenced=true;
        }
        if (!fenced) {
            const auto id=explicitId.match(line); if(id.hasMatch()) pending<<id.captured(1);
            const auto h=heading.match(line);
            if(h.hasMatch()) {
                auto slug=h.captured(2).toLower(); slug.remove(punctuation);
                slug.replace(QRegularExpression("\\s"),"-");
                const int occurrence=occurrences[slug]++;
                if(occurrence) slug+='-'+QString::number(occurrence);
                pending<<slug; pending.removeDuplicates();
                // The heading expression limits this capture to 1..6 '#'.
                const int level = static_cast<int>(h.captured(1).size());
                sections.append({caption(h.captured(2)),line+'\n',pending,level});
                if(sections.size()==1) sections.last().markdown.prepend(intro);
                pending.clear(); continue;
            }
        }
        if(sections.isEmpty()) intro+=line+'\n'; else sections.last().markdown+=line+'\n';
    }
    if(sections.isEmpty()) sections.append({path.section('/',-1),markdown,{},1});
    cache.insert(path,sections); return sections;
}
QVector<HelpPageLink> helpNavigation(const QString &language) {
    const bool zh=language.startsWith("zh"); const QString lang=zh?"zh":"en";
    QVector<HelpPageLink> pages; QSet<QString> seen;
    const auto manifest=QJsonDocument::fromJson(read("navigation.json").toUtf8()).object();
    auto add=[&](HelpPageLink entry) {
        if(seen.contains(entry.path) || !QFile::exists(":/help/"+entry.path)) return;
        if(!zh && !entry.path.contains("_en.",Qt::CaseInsensitive)) {
            const auto body=read(entry.path);
            if(body.contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"))) entry.title+=" (中文)";
        }
        seen.insert(entry.path); pages.append(entry);
    };
    for(const auto &value:manifest["pages"].toArray()) {
        const auto p=value.toObject(); QStringList groups;
        for(const auto &g:p["group"].toObject()[lang].toArray()) groups<<g.toString();
        add({p["title"].toObject()[lang].toString(),p["path"].toObject()[lang].toString(),groups});
    }
    QStringList remaining;
    QDirIterator files(":/help",QDir::Files,QDirIterator::Subdirectories);
    while(files.hasNext()) {
        const auto path=files.next().mid(QString(":/help/").size());
        if (!(path.endsWith(".md",Qt::CaseInsensitive)||path.endsWith(".txt",Qt::CaseInsensitive)||
              path.endsWith("LICENSE")||path.endsWith("COPYING")||path.endsWith("COPYRIGHT"))) continue;
        const bool english=path.contains("_en.",Qt::CaseInsensitive);
        if(zh && english) continue;
        if(!zh && !english && path.endsWith(".md",Qt::CaseInsensitive)) {
            auto stem=path.left(path.size()-3);
            if(QFile::exists(":/help/"+stem+"_en.md")||QFile::exists(":/help/"+stem+"_EN.md")) continue;
        }
        if(!zh && path=="docs/LICENSE_GUIDE_ZH.md") continue;
        if(!seen.contains(path)) remaining<<path;
    }
    remaining.sort();
    for(const auto &path:remaining) {
        const auto sections=helpSections(path); if(sections.isEmpty()) continue;
        const bool methods=path.startsWith("docs/methods/");
        add({sections.first().title,path,methods
            ? QStringList{zh?"软件功能":"Features",zh?"方法参考与来源":"Method references and sources"}
            : QStringList{zh?"关于与引用":"About and references",zh?"项目与许可原文":"Project and license texts"}});
    }
    return pages;
}
} // namespace aster::ui
