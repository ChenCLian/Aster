// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "io/astronomy/la2004_reader.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QResource>
#include <cmath>
#include <stdexcept>

// Must live outside namespaces: also forces the resource object out of static aster_io.
static void initializeAstronomyResources() { Q_INIT_RESOURCE(astronomy); }
namespace aster::astronomy {
namespace {
QByteArray resource(const QString& name)
{
    static const bool initialized = [] { initializeAstronomyResources(); return true; }();
    (void)initialized;
    QFile file(":/astronomy/" + name);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Missing embedded La2004 resource");
    return file.readAll();
}
const QJsonObject& manifest()
{
    static const QJsonObject value = [] {
        const auto doc = QJsonDocument::fromJson(resource("manifest.json"));
        if (!doc.isObject() || doc.object()["sha256"].toString().size() != 64)
            throw std::runtime_error("Invalid La2004 manifest");
        return doc.object();
    }();
    return value;
}
}
La2004Table La2004Table::parse(const QByteArray& ascii)
{
    if (ascii.size() > 8 * 1024 * 1024) throw std::runtime_error("La2004 table exceeds size limit");
    La2004Table table;
    for (auto line : ascii.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        const auto fields = line.simplified().split(' ');
        if (fields.size() != 4) throw std::runtime_error("La2004 row must have four columns");
        double v[4];
        for (int i = 0; i < 4; ++i) {
            bool ok = false;
            v[i] = QByteArray(fields[i]).replace('D', 'E').replace('d', 'e').toDouble(&ok);
            if (!ok || !std::isfinite(v[i])) throw std::runtime_error("Nonfinite or invalid La2004 number");
        }
        // Official nominal past table: t=0,-1,...,-51000 ka; reject gaps/duplicates.
        if (v[0] != -double(table.m_rows.size()) || v[1] < 0 || v[1] >= 1 ||
            v[2] < 0 || v[2] > pi / 2)
            throw std::runtime_error("Invalid La2004 time grid or orbital elements");
        table.m_rows.push_back({v[1], v[2], v[3]});
    }
    if (table.m_rows.size() < 2) throw std::runtime_error("La2004 table has fewer than two rows");
    return table;
}
OrbitalElements La2004Table::at(double age) const
{
    if (!std::isfinite(age) || age < 0 || age > maximumAge())
        throw std::out_of_range("Age is outside the La2004 table; extrapolation is disabled");
    const auto index = size_t(std::floor(age));
    const auto& a = m_rows.at(index);
    const double fraction = age - index;
    if (fraction == 0) return a;
    const auto& b = m_rows.at(index + 1);
    return {a.eccentricity + fraction * (b.eccentricity - a.eccentricity),
            a.obliquity + fraction * (b.obliquity - a.obliquity),
            a.perihelion + fraction * std::remainder(b.perihelion - a.perihelion, 2 * pi)};
}
const La2004Table& la2004()
{
    static const La2004Table table = [] {
        const auto data = resource("INSOLN.LA2004.BTL.ASC");
        if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() !=
            manifest()["sha256"].toString().toLatin1())
            throw std::runtime_error("La2004 table checksum mismatch");
        auto value = La2004Table::parse(data);
        if (value.size() != size_t(manifest()["rows"].toInt()))
            throw std::runtime_error("Incomplete La2004 table");
        return value;
    }();
    return table;
}
QJsonObject la2004Provenance() { return manifest(); }
}
