#include "model/data_types.h"
#include <QSet>
#include <cmath>
#include <limits>
#include <type_traits>

namespace aster {
bool compatibleCurveAxes(const DataRef& a, const DataRef& b)
{
    const auto same = [](const AxisInfo& x, const AxisInfo& y) {
        return x.unit == y.unit && (!x.unit.isEmpty() || x.name == y.name);
    };
    if (const auto* x = dataAs<SeriesData>(a)) {
        const auto* y = dataAs<SeriesData>(b);
        return y && same(x->xAxis, y->xAxis) && same(x->yAxis, y->yAxis);
    }
    if (const auto* x = dataAs<SpectrumData>(a)) {
        const auto* y = dataAs<SpectrumData>(b);
        return y && same(x->frequencyAxis, y->frequencyAxis) && same(x->powerAxis, y->powerAxis);
    }
    return false;
}
namespace {
bool finite(const QVector<double>& values)
{
    for (double value : values) if (!std::isfinite(value)) return false;
    return true;
}
QString pairError(const QVector<double>& x, const QVector<double>& y)
{
    if (x.isEmpty() || x.size() != y.size()) return QStringLiteral("Empty or unequal axis/value lengths");
    if (!finite(x) || !finite(y)) return QStringLiteral("Data contains NaN or infinity");
    return {};
}
}

DataKind dataKind(const DataObject& data)
{
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SeriesData>) return DataKind::Series;
        else if constexpr (std::is_same_v<T, SpectrumData>) return DataKind::Spectrum;
        else if constexpr (std::is_same_v<T, GridData>) return DataKind::Grid;
        else return DataKind::Table;
    }, data);
}

QString dataKindName(DataKind kind)
{
    switch (kind) {
#define KIND_NAME(name) case DataKind::name: return QStringLiteral(#name);
        ASTER_DATA_KINDS(KIND_NAME)
#undef KIND_NAME
    }
    return QStringLiteral("Unknown");
}

QString validateData(const DataRef& data)
{
    if (!data) return QStringLiteral("Null data reference");
    return std::visit([](const auto& value) -> QString {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SeriesData>) {
            return pairError(value.x, value.y);
        } else if constexpr (std::is_same_v<T, SpectrumData>) {
            const auto error = pairError(value.frequency, value.power);
            if (!error.isEmpty()) return error;
            for (double f : value.frequency) if (f < 0) return QStringLiteral("Negative frequency");
            for (double p : value.power) if (p < 0) return QStringLiteral("Negative power");
        } else if constexpr (std::is_same_v<T, GridData>) {
            if (value.rows <= 0 || value.columns <= 0 ||
                value.rows > std::numeric_limits<qsizetype>::max() / value.columns ||
                value.rows != value.y.size() || value.columns != value.x.size() ||
                value.rows * value.columns != value.values.size())
                return QStringLiteral("Grid dimensions do not match axes/values");
            if (!finite(value.x) || !finite(value.y) || !finite(value.values))
                return QStringLiteral("Grid contains NaN or infinity");
        } else {
            if (value.columns.isEmpty()) return QStringLiteral("Table has no columns");
            QSet<QString> names;
            for (const auto& column : value.columns) {
                if (column.isEmpty() || names.contains(column)) return QStringLiteral("Invalid table columns");
                names.insert(column);
            }
            for (const auto& row : value.rows)
                if (row.size() != value.columns.size() || !finite(row))
                    return QStringLiteral("Invalid table row");
        }
        return {};
    }, *data);
}
} // namespace aster
