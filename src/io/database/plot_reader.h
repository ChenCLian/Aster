#ifndef ASTER_PLOT_READER_H
#define ASTER_PLOT_READER_H
#include "io/database/plot_request.h"
#include <QSqlDatabase>
namespace aster
{
// Called on the repository's thread, inside a consistent read transaction.
NodeResult readPlotBlob(QSqlDatabase db, qint64 rowId, const QString &kind,
                        const PlotRequest &request, const CancellationToken &cancel);
} // namespace aster
#endif
