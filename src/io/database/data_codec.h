// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_IO_DATABASE_DATA_CODEC_H
#define ASTER_IO_DATABASE_DATA_CODEC_H
#include "model/data_types.h"
namespace aster {
struct EncodeResult { QByteArray bytes; QString error; bool ok() const { return error.isEmpty(); } };
struct DecodeResult { DataRef data; QString error; bool ok() const { return error.isEmpty() && data; } };
EncodeResult encodeData(const DataRef& data);
DecodeResult decodeData(const QByteArray& bytes);
}
#endif
