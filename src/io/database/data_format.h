// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_DATA_FORMAT_H
#define ASTER_DATA_FORMAT_H
#include <QtGlobal>
namespace aster::DataFormat
{
// Shared by the complete codec and the bounded plot reader. These constants
// describe v1 payloads; v2 is reserved for tables with per-cell validity.
constexpr quint32 magic = 0x41534344;
constexpr quint16 version = 1;
constexpr quint16 maskedTableVersion = 2;
constexpr quint32 elementLimit = 20000000;
constexpr qsizetype byteLimit = 192 * 1024 * 1024;
constexpr quint32 textLimit = 65536;
}
#endif
