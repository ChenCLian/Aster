// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "io/examples/example_catalog.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <algorithm>
#include <stdexcept>

static void initializeExampleResources()
{
    Q_INIT_RESOURCE(examples);
}
namespace aster::examples
{
namespace
{
const QJsonObject &catalog()
{
    static const QJsonObject value = []
    {
        initializeExampleResources();
        QFile file(":/examples/catalog.json");
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Example catalog missing");
        QJsonParseError error;
        auto object = QJsonDocument::fromJson(file.readAll(), &error).object();
        if (error.error != QJsonParseError::NoError || object["format_version"].toInt() != 1)
            throw std::runtime_error("Invalid example catalog");
        return object;
    }();
    return value;
}
} // namespace
QJsonObject description(const QString &id)
{
    for (const auto &entry : catalog()["examples"].toArray())
    {
        const auto example = entry.toObject();
        if (example["id"].toString() == id)
        {
            auto result =
                catalog()["datasets"].toObject()[example["dataset"].toString()].toObject();
            if (result.isEmpty())
                throw std::runtime_error("Example dataset missing");
            for (auto it = example.begin(); it != example.end(); ++it)
                result[it.key()] = it.value();
            return result;
        }
    }
    throw std::invalid_argument("Unknown ODP example");
}
ExampleSeries load(const QString &id, const QString &axis, const std::function<bool()> &cancelled)
{
    ExampleSeries result;
    if (cancelled && cancelled())
    {
        result.cancelled = true;
        return result;
    }
    auto info = description(id);
    if (axis != "age" && axis != "depth")
        throw std::invalid_argument("Unknown example axis");
    QFile file(info["resource"].toString());
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Example table missing");
    const auto bytes = file.readAll();
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() !=
        info["sha256"].toString().toLatin1())
        throw std::runtime_error("Example checksum mismatch");
    const auto text = QString::fromUtf8(bytes);
    const auto marker = text.indexOf("*/\n");
    if (marker < 0)
        throw std::runtime_error("Example metadata terminator missing");
    const auto rows = text.mid(marker + 3).split('\n');
    QStringList expected;
    for (const auto &column : info["columns"].toArray())
        expected.push_back(column.toString());
    if (rows.isEmpty() || rows.front().split('\t') != expected)
        throw std::runtime_error("Example columns do not match catalog");
    const int xColumn = info[axis + "_column"].toInt(-1), yColumn = info["y_column"].toInt(-1);
    if (xColumn < 0 || yColumn < 0 || xColumn >= expected.size() || yColumn >= expected.size())
        throw std::runtime_error("Example column index out of range");
    result.series.xAxis = {info[axis + "_name"].toString(), info[axis + "_unit"].toString()};
    result.series.yAxis = {info["y_name"].toString(), info["y_unit"].toString()};
    int missing = 0, sourceRows = 0;
    double minimumSpacing = 0, maximumSpacing = 0;
    bool increasing = true;
    for (qsizetype i = 1; i < rows.size(); ++i)
    {
        if ((i & 127) == 0 && cancelled && cancelled())
        {
            result.cancelled = true;
            return result;
        }
        if (rows[i].isEmpty())
            continue;
        const auto fields = rows[i].split('\t');
        ++sourceRows;
        if (fields.size() != expected.size())
            throw std::runtime_error("Malformed example row");
        if (fields[xColumn].isEmpty() || fields[yColumn].isEmpty())
        {
            ++missing;
            continue;
        }
        bool xOk = false, yOk = false;
        const double x = fields[xColumn].toDouble(&xOk), y = fields[yColumn].toDouble(&yOk);
        if (!xOk || !yOk || !std::isfinite(x) || !std::isfinite(y))
            throw std::runtime_error("Non-numeric example value");
        if (!result.series.x.isEmpty())
        {
            const double spacing = x - result.series.x.back();
            increasing &= spacing > 0;
            if (result.series.x.size() == 1)
                minimumSpacing = maximumSpacing = spacing;
            else
            {
                minimumSpacing = std::min(minimumSpacing, spacing);
                maximumSpacing = std::max(maximumSpacing, spacing);
            }
        }
        result.series.x.push_back(x);
        result.series.y.push_back(y);
    }
    if (result.series.x.size() < 2)
        throw std::runtime_error("Example has too few valid pairs");
    info["selected_axis"] = axis;
    info["source_rows"] = sourceRows;
    info["omitted_missing_pairs"] = missing;
    info["samples"] = int(result.series.x.size());
    info["minimum_spacing"] = minimumSpacing;
    info["maximum_spacing"] = maximumSpacing;
    info["strictly_increasing"] = increasing;
    info["uniform_spacing"] =
        increasing && maximumSpacing - minimumSpacing <= maximumSpacing * 1e-6;
    info["aster_processing"] =
        "Select X/Y columns and omit missing pairs only; preserve row order and values. No "
        "interpolation, sorting, averaging, smoothing or age conversion.";
    result.provenance = std::move(info);
    return result;
}
} // namespace aster::examples
