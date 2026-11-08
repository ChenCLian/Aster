// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_IO_DATABASE_RESULT_REPOSITORY_H
#define ASTER_IO_DATABASE_RESULT_REPOSITORY_H
#include "core/workflow_executor.h"
#include "io/database/output_reference.h"
#include "io/database/plot_request.h"
#include <QSqlDatabase>
#include <QThread>
#include <QMap>
#include "io/database/record_filter.h"

namespace aster
{
struct SaveReceipt
{
    RunId runId;
    qint64 recordId = 0;
    QString error;
    bool saved() const
    {
        return recordId > 0 && error.isEmpty();
    }
};
struct RecordSummary
{
    qint64 id = 0;
    RunId runId;
    QString sourceLabel, computationState;
    QDateTime startedAt, finishedAt;
    QString displayName, notes;
};
// ID alone is not enough when a database file has been replaced.
using RecordIdentities = QMap<qint64, RunId>;
struct RecordSelectionState
{
    RecordIdentities existing;
    qint64 matching = 0;
    QString error;
};
struct RecordRemoval
{
    RecordIdentities removed;
    qint64 blockedSource = 0, blockingOwner = 0;
    QString error;
    bool ok() const { return error.isEmpty() && blockedSource == 0; }
};
struct RecordList
{
    QVector<RecordSummary> records;
    QString error;
    bool hasMore = false;
};
struct LoadResult
{
    RunResult run;
    QString error;
    QString displayName, notes;
    QVector<PeakMatch> matchingPeaks;
    QMap<NodeId, QVector<OutputInfo>> outputs;
    bool ok() const
    {
        return error.isEmpty();
    }
};

// Owns one SQLite connection on the constructing thread. Never share this
// object across worker threads; create a separate reader on the UI thread.
class ResultRepository
{
  public:
    ResultRepository();
    ~ResultRepository();
    ResultRepository(const ResultRepository &) = delete;
    ResultRepository &operator=(const ResultRepository &) = delete;
    QString open(const QString &path, bool createIfMissing = true);
    SaveReceipt save(const RunResult &run);
    RecordList list(int limit = 100, int offset = 0) const;
    RecordList list(const RecordFilter &filter, int limit = 100, int offset = 0) const;
    // Stable descending-ID pagination: inserting new runs cannot shift later pages.
    RecordList listBefore(const RecordFilter &filter, int limit = 100, qint64 beforeId = 0) const;
    RecordSelectionState identities(const RecordFilter &filter) const;
    RecordSelectionState selectionState(const RecordIdentities &selected, const RecordFilter &filter);
    RecordRemoval remove(const RecordIdentities &selected);
    QStringList frequencyUnits(QString *error = nullptr) const;
    PeakMatches matchingPeaks(qint64 recordId, const RecordFilter &filter) const;
    QString updateMetadata(qint64 recordId, const QString &displayName, const QString &notes);
    QString remove(qint64 recordId);
    LoadResult load(qint64 recordId, bool metadataOnly = false) const;
    OutputLoad loadOutput(const OutputReference &reference) const;
    // Scientific metadata only; no numerical BLOBs and no saved visual style.
    NodeResult loadPlotMetadata(const OutputReference &reference) const;
    // Selected plot family only, bounded before BLOB decoding. No unrelated nodes.
    NodeResult loadPlot(const OutputReference &reference, qint64 budget = 128 * 1024 * 1024) const;

    NodeResult loadPlotPreview(const OutputReference &reference, const PlotRequest &request,
                               const CancellationToken &cancel = {}) const;

  private:
    RecordList queryList(const RecordFilter &filter, int limit, int offset, qint64 beforeId) const;
    SaveReceipt saveImpl(const RunResult &run);
    QString checkThread() const;
    QString m_connection;
    QSqlDatabase m_db;
    Qt::HANDLE m_thread;
};
} // namespace aster
#endif
