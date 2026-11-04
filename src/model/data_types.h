#ifndef ASTER_MODEL_DATA_TYPES_H
#define ASTER_MODEL_DATA_TYPES_H

#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <memory>
#include <variant>

namespace aster {

#define ASTER_DATA_KINDS(X) \
    X(Series)               \
    X(Spectrum)             \
    X(Grid)                 \
    X(Table)

enum class DataKind {
#define DECLARE_KIND(name) name,
    ASTER_DATA_KINDS(DECLARE_KIND)
#undef DECLARE_KIND
};

struct AxisInfo { QString name; QString unit; };
struct SeriesData {
    QVector<double> x, y;
    AxisInfo xAxis, yAxis;
};
struct SpectrumData {
    QVector<double> frequency, power;
    AxisInfo frequencyAxis, powerAxis;
};
struct GridData {
    QVector<double> x, y, values;
    // values[row * columns + column]; row -> y, column -> x.
    qsizetype rows = 0, columns = 0;
    AxisInfo xAxis, yAxis, valueAxis;
};
struct TableData {
    QStringList columns;
    QVector<QVector<double>> rows;
};

using DataObject = std::variant<SeriesData, SpectrumData, GridData, TableData>;
using DataRef = std::shared_ptr<const DataObject>;

// Owns the value; no borrowed buffers or writable pointer is published.
template<class T> DataRef makeData(T value)
{
    return std::make_shared<const DataObject>(std::move(value));
}
template<class T> const T* dataAs(const DataRef& data)
{
    return data ? std::get_if<T>(data.get()) : nullptr;
}

DataKind dataKind(const DataObject& data);
QString dataKindName(DataKind kind);
QString validateData(const DataRef& data); // Empty string means valid.
// Same curve kind and declared axis units; unitless axes must also share names.
// This does not convert units or establish scientific equivalence.
bool compatibleCurveAxes(const DataRef& a, const DataRef& b);

} // namespace aster
#endif
