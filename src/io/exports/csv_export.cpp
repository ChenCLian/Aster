// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "io/exports/csv_export.h"
#include <QSaveFile>
#include <QTextStream>
#include <QLocale>
#include <algorithm>
#include <stdexcept>
#include <type_traits>
namespace aster
{
namespace
{
QString quote(QString text)
{
    text.replace('"', "\"\"");
    return '"' + text + '"';
}
QString label(const AxisInfo &axis)
{
    return axis.unit.isEmpty() ? axis.name : axis.name + " (" + axis.unit + ")";
}
struct Cancelled
{
};
} // namespace
CsvExportResult exportDataCsv(const DataRef &data, const QString &path,
                              const CancellationToken &cancel,
                              const std::function<void(int)> &progress)
{
    if (cancel.isCancelled())
        return {{}, true};
    const auto invalid = validateData(data);
    if (!invalid.isEmpty())
        return {invalid, false};
    if (cancel.isCancelled())
        return {{}, true};
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return {file.errorString(), false};
    try
    {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out.setLocale(QLocale::c());
        out.setRealNumberPrecision(17);
        qint64 total = std::visit(
            [](const auto &d) -> qint64
            {
                using T = std::decay_t<decltype(d)>;
                if constexpr (std::is_same_v<T, SeriesData>)
                    return d.x.size();
                else if constexpr (std::is_same_v<T, SpectrumData>)
                    return d.frequency.size();
                else if constexpr (std::is_same_v<T, GridData>)
                    return d.values.size();
                else
                    return qint64(d.rows.size()) * d.columns.size();
            },
            *data);
        qint64 completed = 0;
        int lastPercent = -1;
        auto checkpoint = [&]
        {
            if (cancel.isCancelled())
                throw Cancelled{};
            const int percent = total ? int(99. * double(completed) / double(total)) : 0;
            if (progress && percent != lastPercent)
            {
                progress(percent);
                lastPercent = percent;
            }
            if (cancel.isCancelled())
                throw Cancelled{};
            if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError)
                throw std::runtime_error("CSV write failed");
        };
        auto tick = [&]
        {
            if ((++completed & 1023) == 0)
                checkpoint();
        };
        checkpoint();
        auto pair = [&](const QVector<double> &x, const QVector<double> &y, const AxisInfo &xa,
                        const AxisInfo &ya)
        {
            out << quote(label(xa)) << ',' << quote(label(ya)) << '\n';
            for (qsizetype i = 0; i < x.size(); ++i)
            {
                out << x[i] << ',' << y[i] << '\n';
                tick();
            }
        };
        if (const auto *d = dataAs<SeriesData>(data))
            pair(d->x, d->y, d->xAxis, d->yAxis);
        else if (const auto *d = dataAs<SpectrumData>(data))
            pair(d->frequency, d->power, d->frequencyAxis, d->powerAxis);
        else if (const auto *d = dataAs<GridData>(data))
        {
            out << quote(label(d->yAxis) + " / " + label(d->xAxis));
            for (qsizetype c = 0; c < d->columns; ++c)
            {
                out << ',' << d->x[c];
                if ((c & 1023) == 0)
                    checkpoint();
            }
            out << '\n';
            for (qsizetype r = 0; r < d->rows; ++r)
            {
                out << d->y[r];
                for (qsizetype c = 0; c < d->columns; ++c)
                {
                    out << ',' << d->values[r * d->columns + c];
                    tick();
                }
                out << '\n';
            }
        }
        else if (const auto *d = dataAs<TableData>(data))
        {
            for (qsizetype i = 0; i < d->columns.size(); ++i)
            {
                if (i)
                    out << ',';
                out << quote(d->columns[i]);
                if ((i & 1023) == 0)
                    checkpoint();
            }
            out << '\n';
            for (qsizetype r = 0; r < d->rows.size(); ++r)
            {
                const auto& row = d->rows[r];
                for (qsizetype i = 0; i < row.size(); ++i)
                {
                    if (i)
                        out << ',';
                    if (d->isDefined(r, i)) out << row[i];
                    tick();
                }
                out << '\n';
            }
        }
        out.flush();
        checkpoint();
        if (!file.commit())
            return {file.errorString(), false};
        // A callback cannot turn an already committed export into a failure.
        if (progress)
        {
            try
            {
                progress(100);
            }
            catch (...)
            {
            }
        }
        return {};
    }
    catch (const Cancelled &)
    {
        file.cancelWriting();
        return {{}, true};
    }
    catch (const std::exception &e)
    {
        file.cancelWriting();
        return {QString::fromUtf8(e.what()), false};
    }
    catch (...)
    {
        file.cancelWriting();
        return {"Unexpected CSV export error", false};
    }
}
} // namespace aster
