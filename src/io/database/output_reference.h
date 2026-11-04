#ifndef ASTER_IO_OUTPUT_REFERENCE_H
#define ASTER_IO_OUTPUT_REFERENCE_H
#include "model/data_types.h"
#include <QJsonObject>
#include <QUuid>
namespace aster
{
// Persisted identity includes run UUID: an ID in a replaced database is never accepted.
struct OutputReference
{
    QString databasePath;
    qint64 recordId = 0;
    QUuid runId, nodeId;
    QString port, kind;
    bool valid() const
    {
        return !databasePath.isEmpty() && recordId > 0 && !runId.isNull() && !nodeId.isNull() &&
               !port.isEmpty();
    }
    QString key() const
    {
        return databasePath + "|" + runId.toString() + "|" + nodeId.toString() + "|" + port;
    }
    QJsonObject toJson() const
    {
        return {{"database", databasePath},
                {"record", QString::number(recordId)},
                {"run", runId.toString()},
                {"node", nodeId.toString()},
                {"port", port},
                {"kind", kind}};
    }
    static OutputReference fromJson(const QJsonObject &o)
    {
        return {o["database"].toString(),   o["record"].toString().toLongLong(),
                QUuid(o["run"].toString()), QUuid(o["node"].toString()),
                o["port"].toString(),       o["kind"].toString()};
    }
};
struct OutputInfo
{
    QString port, kind;
    qint64 bytes = 0;
};
struct OutputLoad
{
    DataRef data;
    QString error;
    bool ok() const
    {
        return data && error.isEmpty();
    }
};
} // namespace aster
Q_DECLARE_METATYPE(aster::OutputReference)
#endif
