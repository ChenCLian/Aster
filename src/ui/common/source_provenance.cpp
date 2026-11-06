// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/source_provenance.h"
#include "ui/common/presentation.h"
namespace aster::ui {
QString sourceProvenanceHtml(const QJsonObject &report)
{
    const auto source = report["example_source"].toObject();
    if (source.isEmpty()) return {};
    const auto field = [&](const char *key) { return source[key].toString().toHtmlEscaped(); };
    const auto chronology = translatedLabel("example_chronology." + source["dataset"].toString(),
                                            source["chronology"].toString());
    return "<p><b>" + text("ui.example_source_data").toHtmlEscaped() + "</b><br>" +
        field("citation") + "<br>" + field("doi") + "<br>" + field("license") + "</p><p>" +
        text("ui.example_sample_counts").arg(source["samples"].toInt())
            .arg(source["omitted_missing_pairs"].toInt()).toHtmlEscaped() + "<br>" +
        chronology.toHtmlEscaped() + "</p>";
}
}
