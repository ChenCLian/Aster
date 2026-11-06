// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_IO_FILES_SERIES_FILE_READER_H
#define ASTER_IO_FILES_SERIES_FILE_READER_H
#include "core/task.h"

namespace aster
{
struct SeriesFileOptions
{
    QString delimiter = "auto"; // auto, comma, tab, whitespace
    bool header = true;
    bool autoHeader = false;
    QString xUnit, yUnit;
    int xColumn = 0, yColumn = 1; // zero based
    bool skipInvalidRows = false;
    qsizetype maxRows = 1000000;
};
struct FilePreviewRow
{
    qsizetype line = 0;
    QStringList fields;
    bool wellFormed = true;
};
struct FileProbe
{
    QStringList columns;
    QVector<FilePreviewRow> preview;
    QString delimiter;
    bool header = false;
    QString error;
    bool ok() const
    {
        return error.isEmpty() && columns.size() >= 2;
    }
};
// Bounded inspection of columns and at most five data rows. Shares the real parser.
FileProbe probeSeriesFile(const QString &path, const SeriesFileOptions &options = {});
QJsonObject fileOptionsToJson(const SeriesFileOptions &options);
QString applyFileOptions(const QJsonObject &value, SeriesFileOptions &options);
struct FileReadResult
{
    DataRef data;
    QJsonObject report;
    QString error;
    QStringList warnings;
    bool ok() const
    {
        return error.isEmpty() && data;
    }
};
FileReadResult readSeriesFile(const QString &path, const SeriesFileOptions &options,
                              CancellationToken cancellation = {});
// Parse an already captured byte snapshot using the identical file parser.
FileReadResult readSeriesBytes(const QByteArray &bytes, const QString &sourceLabel,
                               const SeriesFileOptions &options, CancellationToken cancellation = {});
} // namespace aster
#endif
