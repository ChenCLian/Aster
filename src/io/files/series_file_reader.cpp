#include "io/files/series_file_reader.h"
#include <QFile>
#include <QBuffer>
#include <QFileInfo>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QTextStream>
#include <QStringConverter>
#include <cmath>
#include <algorithm>

namespace aster
{
namespace
{
bool splitFields(const QString &line, QChar delimiter, QStringList &fields)
{
    QString field;
    bool quoted = false, closed = false;
    for (qsizetype i = 0; i < line.size(); ++i)
    {
        const auto c = line[i];
        if (quoted)
        {
            if (c == QLatin1Char('"'))
            {
                if (i + 1 < line.size() && line[i + 1] == QLatin1Char('"'))
                {
                    field += c;
                    ++i;
                }
                else
                {
                    quoted = false;
                    closed = true;
                }
            }
            else
                field += c;
        }
        else if (c == delimiter)
        {
            fields.push_back(field.trimmed());
            field.clear();
            closed = false;
        }
        else if (c == QLatin1Char('"') && field.trimmed().isEmpty() && !closed)
        {
            field.clear();
            quoted = true;
        }
        else if (closed && !c.isSpace())
            return false;
        else if (!closed)
            field += c;
    }
    if (quoted)
        return false; // Multiline quoted fields are deliberately not accepted.
    fields.push_back(field.trimmed());
    return true;
}
QString detectDelimiter(const QString &line, const QString &configured)
{
    if (configured != "auto")
        return configured;
    return line.contains('\t') ? "tab" : line.contains(',') ? "comma" : "whitespace";
}
bool fieldsFor(const QString &line, const QString &delimiter, QStringList &fields)
{
    if (delimiter == "whitespace")
    {
        // Compile once per reader thread, not once per input row. Recreating
        // the expression here dominates parsing of large whitespace files.
        static thread_local const QRegularExpression whitespace(QStringLiteral("\\s+"));
        fields = line.trimmed().split(whitespace, Qt::SkipEmptyParts);
        return true;
    }
    return splitFields(line, delimiter == "comma" ? QLatin1Char(',') : QLatin1Char('\t'), fields);
}
bool looksLikeHeader(const QStringList &fields, const SeriesFileOptions &options)
{
    QLocale locale = QLocale::c();
    locale.setNumberOptions(QLocale::RejectGroupSeparator);
    // Unselected columns may contain labels. They must not make a numeric
    // first row disappear when X/Y themselves are valid numbers.
    for (int column : {options.xColumn, options.yColumn})
    {
        if (column < 0 || column >= fields.size())
            continue;
        bool ok;
        locale.toDouble(fields[column], &ok);
        if (!ok)
            return true;
    }
    return false;
}
} // namespace
QJsonObject fileOptionsToJson(const SeriesFileOptions &o)
{
    return {{"delimiter", o.delimiter},    {"header", o.header},
            {"auto_header", o.autoHeader}, {"x_column", o.xColumn},
            {"y_column", o.yColumn},       {"skip_invalid_rows", o.skipInvalidRows},
            {"x_unit", o.xUnit},           {"y_unit", o.yUnit}};
}
QString applyFileOptions(const QJsonObject &v, SeriesFileOptions &o)
{
    auto candidate = o;
    for (const auto &key : {"header", "auto_header", "skip_invalid_rows"})
        if (v.contains(key) && !v[key].isBool())
            return QString("Invalid boolean: %1").arg(key);
    for (const auto &key : {"delimiter", "x_unit", "y_unit"})
        if (v.contains(key) && !v[key].isString())
            return QString("Invalid text: %1").arg(key);
    for (const auto &key : {"x_column", "y_column"})
        if (v.contains(key) &&
            (!v[key].isDouble() || v[key].toDouble() < 0 || v[key].toDouble() > 10000 ||
             std::floor(v[key].toDouble()) != v[key].toDouble()))
            return QString("Invalid column: %1").arg(key);
    if (v.contains("delimiter"))
        candidate.delimiter = v["delimiter"].toString();
    if (!QStringList{"auto", "comma", "tab", "whitespace"}.contains(candidate.delimiter))
        return "Unknown delimiter";
    if (v.contains("header"))
        candidate.header = v["header"].toBool();
    if (v.contains("auto_header"))
        candidate.autoHeader = v["auto_header"].toBool();
    if (v.contains("skip_invalid_rows"))
        candidate.skipInvalidRows = v["skip_invalid_rows"].toBool();
    if (v.contains("x_column"))
        candidate.xColumn = v["x_column"].toInt();
    if (v.contains("y_column"))
        candidate.yColumn = v["y_column"].toInt();
    if (v.contains("x_unit"))
        candidate.xUnit = v["x_unit"].toString().trimmed();
    if (v.contains("y_unit"))
        candidate.yUnit = v["y_unit"].toString().trimmed();
    o = candidate;
    return {};
}
FileProbe probeSeriesFile(const QString &path, const SeriesFileOptions &options)
{
    FileProbe result;
    auto checked = options;
    result.error = applyFileOptions(fileOptionsToJson(options), checked);
    if (!result.error.isEmpty())
        return result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        result.error = file.errorString();
        return result;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    qsizetype scanned = 0;
    for (int n = 0; n < 256 && !stream.atEnd(); ++n)
    {
        const auto line = stream.readLine(65537);
        scanned += line.size();
        if (line.size() > 65536 || scanned > 262144)
            break;
        if (line.trimmed().isEmpty() || line.trimmed().startsWith('#'))
            continue;
        const bool first = result.columns.isEmpty();
        if (first) result.delimiter = detectDelimiter(line, options.delimiter);
        QStringList fields;
        const bool wellFormed = fieldsFor(line, result.delimiter, fields);
        if (first && (!wellFormed || fields.size() < 2))
        {
            result.error = "First data line must contain at least two valid columns";
            return result;
        }
        if (first)
        {
            result.header = options.autoHeader ? looksLikeHeader(fields, options) : options.header;
            for (qsizetype i = 0; i < fields.size(); ++i)
                result.columns.push_back(result.header && !fields[i].isEmpty()
                                             ? fields[i]
                                             : QString("Column %1").arg(i + 1));
            if (result.header) continue;
        }
        result.preview.push_back({n + 1, std::move(fields), wellFormed});
        if (result.preview.size() >= 5) break;
    }
    if (result.columns.isEmpty())
        result.error = "No readable data line in preview (256 lines / 256 KiB limit)";
    return result;
}
static FileReadResult readSeriesDevice(QIODevice &device, const QString &path,
                                       const SeriesFileOptions &options, CancellationToken cancel)
{
    FileReadResult result;
    const auto fail = [&](ErrorCode code, QJsonObject context = {})
    {
        context.insert("path", path);
        result.report.insert("diagnostic", makeError(code, context, result.error).toJson());
        return result;
    };
    if (path.isEmpty() || options.xColumn < 0 || options.yColumn < 0 || options.maxRows < 2)
    {
        result.error = "Invalid file path/column/row limit";
        return fail(ErrorCode::FileOptions, {{"x_column", options.xColumn + 1}, {"y_column", options.yColumn + 1}, {"row_limit", double(options.maxRows)}});
    }
    if (!QStringList{"auto", "comma", "tab", "whitespace"}.contains(options.delimiter))
    {
        result.error = "Unknown delimiter";
        return fail(ErrorCode::FileOptions, {{"delimiter", options.delimiter}});
    }
    if (!device.isOpen())
    {
        result.error = device.errorString();
        return fail(ErrorCode::FileOpen);
    }
    QTextStream stream(&device);
    stream.setEncoding(QStringConverter::Utf8);
    SeriesData series;
    series.xAxis = {"X", options.xUnit};
    series.yAxis = {"Y", options.yUnit};
    bool actualHeader = options.header;
    result.report = fileOptionsToJson(options);
    QString delimiter = options.delimiter;
    qsizetype physicalLine = 0, skipped = 0;
    bool first = true;
    QLocale locale = QLocale::c();
    locale.setNumberOptions(QLocale::RejectGroupSeparator);
    while (!stream.atEnd())
    {
        if (cancel.isCancelled())
        {
            result.error = "Cancellation requested";
            return fail(ErrorCode::Cancelled);
        }
        const auto line = stream.readLine(65537);
        ++physicalLine;
        if (line.size() > 65536)
        {
            result.error = QString("File line exceeds 65536 characters at line %1").arg(physicalLine);
            return fail(ErrorCode::FileLineLimit, {{"line", double(physicalLine)}, {"maximum", 65536}});
        }
        if (line.trimmed().isEmpty() || line.trimmed().startsWith(QLatin1Char('#')))
            continue;
        delimiter = detectDelimiter(line, delimiter);
        QStringList fields;
        const bool wellFormed = fieldsFor(line, delimiter, fields);
        if (first && options.autoHeader)
            actualHeader = looksLikeHeader(fields, options);
        const auto columns = std::max(options.xColumn, options.yColumn);
        if (first && actualHeader)
        {
            first = false;
            if (!wellFormed || fields.size() <= columns)
            {
                result.error = "Header does not contain the selected columns";
                return fail(ErrorCode::FileColumns, {{"line", double(physicalLine)}, {"column_count", double(fields.size())}, {"x_column", options.xColumn + 1}, {"y_column", options.yColumn + 1}, {"well_formed", wellFormed}});
            }
            series.xAxis.name = fields[options.xColumn];
            series.yAxis.name = fields[options.yColumn];
            continue;
        }
        first = false;
        bool xOk = false, yOk = false;
        double x = 0, y = 0;
        if (wellFormed && fields.size() > columns)
        {
            // C locale; reject thousands separators to avoid ambiguous numeric data.
            x = locale.toDouble(fields[options.xColumn], &xOk);
            y = locale.toDouble(fields[options.yColumn], &yOk);
        }
        if (!xOk || !yOk || !std::isfinite(x) || !std::isfinite(y))
        {
            if (options.skipInvalidRows)
            {
                ++skipped;
                continue;
            }
            const auto xValue = fields.value(options.xColumn).left(160);
            const auto yValue = fields.value(options.yColumn).left(160);
            result.report["input_error"] = QJsonObject{
                {"code", "invalid_numeric_row"}, {"line", double(physicalLine)},
                {"x_column", options.xColumn}, {"y_column", options.yColumn},
                {"x_value", xValue}, {"y_value", yValue}, {"delimiter", delimiter},
                {"well_formed", wellFormed}, {"raw_line", line.left(240)}};
            result.error = QString("Invalid numeric row at line %1: X(column %2)=\"%3\", "
                                   "Y(column %4)=\"%5\"; delimiter=%6")
                               .arg(physicalLine).arg(options.xColumn + 1).arg(xValue)
                               .arg(options.yColumn + 1).arg(yValue).arg(delimiter);
            return fail(ErrorCode::FileNumeric, {{"line", double(physicalLine)}, {"x_column", options.xColumn + 1}, {"y_column", options.yColumn + 1}, {"x_value", xValue}, {"y_value", yValue}, {"delimiter", delimiter}, {"well_formed", wellFormed}});
        }
        if (series.x.size() >= options.maxRows)
        {
            result.error = "File row limit exceeded";
            return fail(ErrorCode::FileRowLimit, {{"line", double(physicalLine)}, {"maximum", double(options.maxRows)}});
        }
        series.x.push_back(x);
        series.y.push_back(y);
    }
    if (stream.status() != QTextStream::Ok)
    {
        result.error = "Text decoding/read failed";
        return fail(ErrorCode::FileRead, {{"line", double(physicalLine)}});
    }
    if (series.x.size() < 2)
    {
        result.error = "At least two valid rows are required";
        return fail(ErrorCode::FileRows, {{"actual", double(series.x.size())}, {"minimum", 2}, {"skipped_rows", double(skipped)}});
    }
    result.report = {{"path", path},
                     {"size_bytes", static_cast<double>(device.size())},
                     {"rows", static_cast<double>(series.x.size())},
                     {"skipped_rows", static_cast<double>(skipped)},
                     {"delimiter", delimiter},
                     {"x_column", options.xColumn},
                     {"y_column", options.yColumn},
                     {"header", actualHeader},
                     {"x_unit", options.xUnit},
                     {"y_unit", options.yUnit}};
    if (skipped)
        result.warnings.push_back(
            QString("Skipped %1 invalid rows under the selected policy").arg(skipped));
    result.data = makeData(std::move(series));
    return result;
}
FileReadResult readSeriesFile(const QString &path, const SeriesFileOptions &options,
                              CancellationToken cancel)
{
    QFile file(path);
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    auto result=readSeriesDevice(file,path,options,cancel);
    if (result.ok()) {
        const QFileInfo info(file);
        result.report["path"]=info.absoluteFilePath();
        result.report["modified_utc"]=info.lastModified().toUTC().toString(Qt::ISODateWithMs);
    }
    return result;
}
FileReadResult readSeriesBytes(const QByteArray &bytes, const QString &sourceLabel,
                               const SeriesFileOptions &options, CancellationToken cancel)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    return readSeriesDevice(buffer,sourceLabel,options,cancel);
}
} // namespace aster
