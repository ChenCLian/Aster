// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_IO_ASTRONOMY_LA2004_READER_H
#define ASTER_IO_ASTRONOMY_LA2004_READER_H
#include "math/astronomy/orbital_elements.h"
#include <QByteArray>
#include <QJsonObject>
#include <vector>
namespace aster::astronomy {
// Immutable after construction; shared safely by independent workflow workers.
class La2004Table {
public:
    static La2004Table parse(const QByteArray& ascii);
    OrbitalElements at(double ageKaBeforeJ2000) const;
    size_t size() const { return m_rows.size(); }
    double maximumAge() const { return double(m_rows.size() - 1); }
private:
    La2004Table() = default;
    std::vector<OrbitalElements> m_rows;
};
// Lazy, once per process, hash-verified embedded table. Throws on corruption.
const La2004Table& la2004();
QJsonObject la2004Provenance();
}
#endif
