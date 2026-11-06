// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/scientific_registry.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

// Read descriptors only: no task execution, database access, settings or GUI.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QJsonArray tasks;
    for (const auto &d : aster::makeScientificRegistry().descriptors())
    {
        QJsonArray parameters, outputs;
        for (const auto &p : d.parameters)
            parameters.append(QJsonObject{{"key", p.key}, {"label", p.label},
                {"help", p.help}, {"default", p.defaultValue},
                {"choices", QJsonArray::fromStringList(p.choices)},
                {"numeric", p.kind == aster::ParameterKind::Number ||
                            p.kind == aster::ParameterKind::Integer},
                {"minimum", p.minimum}, {"maximum", p.maximum},
                {"visibleWhen", p.visibleWhen}});
        for (const auto &o : d.outputs)
            outputs.append(o.key);
        tasks.append(QJsonObject{{"key", d.key}, {"label", d.label},
            {"parameters", parameters}, {"outputs", outputs}});
    }
    const QJsonObject catalog{{"schema", 1}, {"version", ASTER_VERSION}, {"tasks", tasks}};
    QTextStream out(stdout);
    out << QJsonDocument(catalog).toJson(QJsonDocument::Indented);
    out.flush();
    return out.status() == QTextStream::Ok ? 0 : 1;
}
