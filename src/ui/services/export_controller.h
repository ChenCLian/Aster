#ifndef ASTER_UI_EXPORT_CONTROLLER_H
#define ASTER_UI_EXPORT_CONTROLLER_H
#include "io/exports/csv_export.h"
#include "io/database/output_reference.h"
#include <QObject>
class QThread;
namespace aster::ui
{
class ExportController : public QObject
{
    Q_OBJECT
  public:
    explicit ExportController(QObject *parent = nullptr) : QObject(parent) {}
    ~ExportController() override;
    bool start(DataRef data, const QString &path);
    bool startOutput(OutputReference reference, const QString &path);
    bool busy() const
    {
        return m_worker != nullptr;
    }
    void cancel()
    {
        m_cancel.cancel();
    }
  signals:
    void busyChanged(bool busy);
    void progressChanged(int percent);
    void finished(QString path, QString error, bool cancelled);

  private:
    bool launch(DataRef data, OutputReference reference, const QString &path);
    QThread *m_worker = nullptr;
    CancellationToken m_cancel;
};
} // namespace aster::ui
#endif
