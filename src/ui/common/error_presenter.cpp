// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/error_presenter.h"
#include "ui/common/presentation.h"
#include <QRegularExpression>
#include <algorithm>
namespace aster::ui
{
namespace
{
QString scalar(const QJsonValue &value)
{
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 17);
    if (value.isBool()) return text(value.toBool() ? "diagnostic.yes" : "diagnostic.no");
    if (value.isNull()) return text("diagnostic.unavailable");
    return value.toString();
}
QString optionalLabel(const QString &key)
{
    const auto value = translatedLabel(key, key);
    return value == key ? QString() : value;
}
QString expand(const QString &pattern, const QJsonObject &context)
{
    // Single pass: braces in imported values are data, never new placeholders.
    static const QRegularExpression fields(QStringLiteral("\\{([a-z_]+)\\}"));
    auto matches = fields.globalMatch(pattern);
    QString out;
    qsizetype offset = 0;
    while (matches.hasNext())
    {
        const auto match = matches.next();
        out += pattern.mid(offset, match.capturedStart() - offset);
        out += context.contains(match.captured(1)) ? scalar(context.value(match.captured(1)))
                                                : text("diagnostic.unavailable");
        offset = match.capturedEnd();
    }
    return out + pattern.mid(offset);
}
ErrorInfo legacyError(const NodeResult &result)
{
    // Read-only compatibility for records written before structured diagnostics.
    const auto detail = result.report.value("input_error").toObject();
    if (detail.value("code").toString() == "invalid_numeric_row")
    {
        auto context = detail;
        context.remove("code");
        context["x_column"] = detail["x_column"].toInt() + 1;
        context["y_column"] = detail["y_column"].toInt() + 1;
        return makeError(ErrorCode::FileNumeric, context, result.error);
    }
    if (result.state == NodeState::Blocked) return makeError(ErrorCode::UpstreamFailed);
    if (result.state == NodeState::Cancelled) return makeError(ErrorCode::Cancelled);
    if (result.error == "X must be strictly increasing; run preprocessing first")
        return makeError(ErrorCode::XDescending, {}, result.error);
    if (result.error == "Sampling is not uniform; resample before spectral analysis")
        return makeError(ErrorCode::XNonUniform, {}, result.error);
    const auto line = QRegularExpression("^Invalid numeric row at line (\\d+)").match(result.error);
    if (line.hasMatch()) return makeError(ErrorCode::FileNumeric, {{"line", line.captured(1)}}, result.error);
    return result.error.isEmpty() ? ErrorInfo() : makeError(ErrorCode::Unknown, {}, result.error);
}
}
ErrorPresentation presentError(const ErrorInfo &raw, const QString &fallback)
{
    // Apply the same bounded scalar contract to external and built-in diagnostics.
    const auto error = ErrorInfo::fromJson(raw.toJson());
    if (error.isEmpty()) return {fallback, {}, fallback};
    const auto prefix = "diagnostic." + error.code;
    auto summary = optionalLabel(prefix + ".summary");
    if (error.code == errorCodeName(ErrorCode::Unknown)) summary.clear();
    if (summary.isEmpty()) summary = fallback.isEmpty() ? errorFallback(error) : fallback;
    else summary = expand(summary, error.context);
    const auto suggestion = expand(optionalLabel(prefix + ".suggestion"), error.context);
    QStringList lines{summary, text("diagnostic.code") + ": " + error.code};
    QStringList trace;
    auto fields = error.context.keys();
    const QStringList priority{"line", "x_column", "x_value", "y_column", "y_value",
                               "point_before", "x_before", "point", "x"};
    const auto rank = [&](const QString &key)
    {
        const auto index = priority.indexOf(key);
        return index < 0 ? priority.size() : index;
    };
    std::stable_sort(fields.begin(), fields.end(),
                     [&](const QString &a, const QString &b) { return rank(a) < rank(b); });
    for (const auto &key : fields)
    {
        auto value = scalar(error.context.value(key));
        if (key == "method") value = translatedLabel("method." + value, value);
        if (key == "input_port" || key == "output_port") value = portLabel(value);
        const auto entry = translatedLabel("diagnostic.field." + key, key) + ": " + value;
        if (key.endsWith("_id") || key == "method" || key == "input_port")
            trace << entry;
        else
            lines << entry;
    }
    if (!suggestion.isEmpty()) lines << "\n" + text("diagnostic.suggestion") + "\n" + suggestion;
    if (!error.technical.isEmpty()) lines << "\n" + text("diagnostic.technical") + "\n" + error.technical;
    if (!trace.isEmpty()) lines << "\n" + text("diagnostic.trace") + "\n" + trace.join('\n');
    return {summary, suggestion, lines.join('\n')};
}
ErrorPresentation presentError(const NodeResult &result)
{
    auto diagnostic = result.diagnostic();
    if (diagnostic.isEmpty() || diagnostic.code == errorCodeName(ErrorCode::Unknown))
    {
        const auto legacy = legacyError(result);
        if (!legacy.isEmpty() && legacy.code != errorCodeName(ErrorCode::Unknown)) diagnostic = legacy;
    }
    return presentError(diagnostic, result.error);
}
} // namespace aster::ui
