// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_FILE_PREVIEW_LOADER_H
#define ASTER_UI_FILE_PREVIEW_LOADER_H
#include "io/files/series_file_reader.h"
#include "ui/results/preview_data.h"
#include <QObject>
#include <QTimer>
class QThread;
namespace aster::ui
{
struct FilePreviewData
{
    SeriesData samples;
    qsizetype rows = 0, skippedRows = 0;
    NodeResult failure;
    bool ok() const { return rows > 0; }
};

// A single, cancellable reader for the currently selected file. No database or workflow writes.
class FilePreviewLoader : public QObject
{
    Q_OBJECT
  public:
    enum class State { Inactive, Empty, Waiting, Loading, Ready, Failed };
    static constexpr qsizetype PointBudget = PreviewLimits::seriesPoints;
    explicit FilePreviewLoader(QObject *parent = nullptr);
    ~FilePreviewLoader() override;
    void setInput(const QString &path, const QJsonObject &options, bool ready);
    void setActive(bool active);
    State state() const { return m_state; }
    QString path() const { return m_path; }
    std::shared_ptr<const FilePreviewData> result() const { return m_result; }
  signals:
    void changed();
    void readStarted(const QString &path);
  private:
    void resetRequest();
    void start();
    bool m_active = false, m_ready = false, m_due = false;
    State m_state = State::Inactive;
    QString m_path;
    QJsonObject m_options;
    quint64 m_generation = 0;
    CancellationToken m_cancel;
    QTimer m_debounce;
    QThread *m_worker = nullptr;
    std::shared_ptr<const FilePreviewData> m_result;
};
} // namespace aster::ui
#endif
