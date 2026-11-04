#include "io/database/data_format.h"
#include "io/database/data_codec.h"
#include <QBuffer>
#include <QDataStream>
#include <stdexcept>
#include <type_traits>

namespace aster {
namespace {
using namespace DataFormat;
void configure(QDataStream& stream)
{ stream.setVersion(QDataStream::Qt_6_0); stream.setByteOrder(QDataStream::LittleEndian); stream.setFloatingPointPrecision(QDataStream::DoublePrecision); }
void writeText(QDataStream& out, const QString& text)
{
    const auto bytes = text.toUtf8();
    if (bytes.size() > DataFormat::textLimit) throw std::runtime_error("Metadata string too long");
    out << static_cast<quint32>(bytes.size()); out.writeRawData(bytes.constData(), bytes.size());
}
QString readText(QDataStream& in)
{
    quint32 size = 0; in >> size;
    if (in.status() != QDataStream::Ok || size > DataFormat::textLimit || size > in.device()->bytesAvailable()) throw std::runtime_error("Invalid metadata string length");
    QByteArray bytes(size, Qt::Uninitialized);
    if (in.readRawData(bytes.data(), size) != static_cast<int>(size)) throw std::runtime_error("Truncated metadata string");
    return QString::fromUtf8(bytes);
}
void writeAxis(QDataStream& out, const AxisInfo& axis) { writeText(out, axis.name); writeText(out, axis.unit); }
AxisInfo readAxis(QDataStream& in) { const auto name = readText(in); const auto unit = readText(in); return {name, unit}; }
void writeValues(QDataStream& out, const QVector<double>& values, quint64& budget)
{
    budget += values.size(); if (budget > elementLimit) throw std::runtime_error("Numerical payload too large");
    out << static_cast<quint32>(values.size()); for (double value : values) out << value;
}
QVector<double> readValues(QDataStream& in, quint64& budget)
{
    quint32 size = 0; in >> size; budget += size;
    if (in.status() != QDataStream::Ok || budget > elementLimit || static_cast<quint64>(size) * 8 > static_cast<quint64>(in.device()->bytesAvailable()))
        throw std::runtime_error("Invalid numerical payload length");
    QVector<double> values(size); for (auto& value : values) in >> value; return values;
}
}
EncodeResult encodeData(const DataRef& data)
{
    EncodeResult result; result.error = validateData(data); if (!result.ok()) return result;
    try {
        QDataStream out(&result.bytes, QIODevice::WriteOnly); configure(out); out << magic << DataFormat::version;
        quint64 budget = 0;
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SeriesData>) {
                out << quint8(1); writeAxis(out, value.xAxis); writeAxis(out, value.yAxis);
                writeValues(out, value.x, budget); writeValues(out, value.y, budget);
            } else if constexpr (std::is_same_v<T, SpectrumData>) {
                out << quint8(2); writeAxis(out, value.frequencyAxis); writeAxis(out, value.powerAxis);
                writeValues(out, value.frequency, budget); writeValues(out, value.power, budget);
            } else if constexpr (std::is_same_v<T, GridData>) {
                out << quint8(3) << quint32(value.rows) << quint32(value.columns);
                writeAxis(out, value.xAxis); writeAxis(out, value.yAxis); writeAxis(out, value.valueAxis);
                writeValues(out, value.x, budget); writeValues(out, value.y, budget); writeValues(out, value.values, budget);
            } else {
                if (value.columns.size() > 10000 || value.rows.size() > elementLimit) throw std::runtime_error("Table dimensions too large");
                out << quint8(4) << quint32(value.columns.size()) << quint32(value.rows.size());
                for (const auto& column : value.columns) writeText(out, column);
                for (const auto& row : value.rows) writeValues(out, row, budget);
            }
        }, *data);
        if (out.status() != QDataStream::Ok || result.bytes.size() > byteLimit) throw std::runtime_error("Payload encoding failed or exceeded limit");
    } catch (const std::exception& e) { result.error = QString::fromUtf8(e.what()); result.bytes.clear(); }
    return result;
}
DecodeResult decodeData(const QByteArray& bytes)
{
    DecodeResult result;
    try {
        if (bytes.size() > byteLimit) throw std::runtime_error("Encoded payload exceeds limit");
        QDataStream in(bytes); configure(in);
        quint32 readMagic = 0; quint16 version = 0; quint8 type = 0; quint64 budget = 0;
        in >> readMagic >> version >> type;
        if (readMagic != magic || version != DataFormat::version) throw std::runtime_error("Unknown data encoding/version");
        if (type == 1) {
            SeriesData value; value.xAxis = readAxis(in); value.yAxis = readAxis(in);
            value.x = readValues(in, budget); value.y = readValues(in, budget); result.data = makeData(std::move(value));
        } else if (type == 2) {
            SpectrumData value; value.frequencyAxis = readAxis(in); value.powerAxis = readAxis(in);
            value.frequency = readValues(in, budget); value.power = readValues(in, budget); result.data = makeData(std::move(value));
        } else if (type == 3) {
            quint32 rows = 0, columns = 0; in >> rows >> columns;
            if (!rows || !columns || quint64(rows) * columns > elementLimit) throw std::runtime_error("Invalid grid size");
            GridData value; value.rows = rows; value.columns = columns;
            value.xAxis = readAxis(in); value.yAxis = readAxis(in); value.valueAxis = readAxis(in);
            value.x = readValues(in, budget); value.y = readValues(in, budget); value.values = readValues(in, budget);
            result.data = makeData(std::move(value));
        } else if (type == 4) {
            quint32 columns = 0, rows = 0; in >> columns >> rows;
            if (!columns || columns > 10000 || quint64(rows) * columns > elementLimit) throw std::runtime_error("Invalid table size");
            TableData value;
            for (quint32 i = 0; i < columns; ++i) value.columns.push_back(readText(in));
            // Each row has a length prefix and exactly `columns` doubles.
            // Reject truncated/forged counts before allocating millions of rows.
            if (quint64(rows) * (4 + quint64(columns) * 8) > quint64(in.device()->bytesAvailable()))
                throw std::runtime_error("Truncated table rows");
            for (quint32 i = 0; i < rows; ++i) {
                auto row = readValues(in, budget);
                if (row.size() != columns) throw std::runtime_error("Table row width mismatch");
                value.rows.push_back(std::move(row));
            }
            result.data = makeData(std::move(value));
        } else throw std::runtime_error("Unknown data type");
        if (in.status() != QDataStream::Ok || !in.atEnd()) throw std::runtime_error("Truncated or trailing payload bytes");
        result.error = validateData(result.data);
        if (!result.error.isEmpty()) result.data.reset();
    } catch (const std::exception& e) { result.error = QString::fromUtf8(e.what()); result.data.reset(); }
    return result;
}
}
