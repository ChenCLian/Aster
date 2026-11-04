#ifndef ASTER_DATA_FORMAT_H
#define ASTER_DATA_FORMAT_H
#include <QtGlobal>
namespace aster::DataFormat
{
// Shared by the complete codec and the bounded plot reader. These constants
// describe the existing v1 format; changing them requires a format revision.
constexpr quint32 magic = 0x41534344;
constexpr quint16 version = 1;
constexpr quint32 elementLimit = 20000000;
constexpr qsizetype byteLimit = 192 * 1024 * 1024;
constexpr quint32 textLimit = 65536;
}
#endif
