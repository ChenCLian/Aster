#ifndef ASTER_IO_CSV_EXPORT_H
#define ASTER_IO_CSV_EXPORT_H
#include "core/task.h"
namespace aster
{
struct CsvExportResult
{
    QString error;
    bool cancelled = false;
    bool ok() const
    {
        return error.isEmpty() && !cancelled;
    }
};
// Own the immutable input for the entire operation. Commit only a complete file.
CsvExportResult exportDataCsv(const DataRef &data, const QString &path,
                              const CancellationToken &cancel = {},
                              const std::function<void(int)> &progress = {});
} // namespace aster
#endif
