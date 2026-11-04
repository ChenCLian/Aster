#ifndef ASTER_UI_RESULT_LOADER_H
#define ASTER_UI_RESULT_LOADER_H
#include "io/database/result_repository.h"
#include <QObject>
class QThread;
namespace aster::ui
{
class ResultLoader : public QObject
{
    Q_OBJECT
  public:
    explicit ResultLoader(QObject *parent = nullptr) : QObject(parent) {}
    ~ResultLoader() override;
    void request(const QString &path, qint64 id, RecordFilter filter = {});
    void invalidate();
  signals:
    void ready(qint64 id, std::shared_ptr<const aster::LoadResult> result);

  private:
    void start();
    QThread *m_worker = nullptr;
    quint64 m_generation = 0;
    bool m_pending = false;
    QString m_path;
    RecordFilter m_filter;
    qint64 m_id = 0;
};
} // namespace aster::ui
#endif
