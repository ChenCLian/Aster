// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_AR1_REFERENCE_CACHE_H
#define ASTER_PROCESSING_AR1_REFERENCE_CACHE_H
#include "math/statistics/ar1_global.h"
#include <QString>
namespace aster {
struct Ar1ReferenceAccess {
    std::shared_ptr<const numeric::Ar1ReferenceBank> bank;
    QString key, warning;
    bool hit = false, saved = false;
};
// File cache owns no data-dependent spectra. Immutable banks, atomic commit,
// per-key interprocess locking, bounded retention. Empty directory disables it.
class Ar1ReferenceCache {
public:
    explicit Ar1ReferenceCache(QString directory);
    static QString defaultDirectory();
    Ar1ReferenceAccess acquire(std::size_t samples,const numeric::Ar1GlobalOptions&,
        const std::function<bool()>& cancelled = {},
        const std::function<void(int,const QString&)>& progress = {}) const;
private:
    QString m_directory;
};
}
#endif
