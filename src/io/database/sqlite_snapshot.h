#ifndef ASTER_SQLITE_SNAPSHOT_H
#define ASTER_SQLITE_SNAPSHOT_H
#include <QSqlDatabase>
#include <QString>
#include <QSqlQuery>
#include <stdexcept>

namespace aster::database_detail
{
// Maintenance-only connection; opening a source never creates or upgrades it.
class SqliteFile final
{
  public:
    SqliteFile(const QString &path, bool readOnly = true);
    ~SqliteFile();
    SqliteFile(const SqliteFile &) = delete;
    SqliteFile &operator=(const SqliteFile &) = delete;
    QSqlDatabase db;

  private:
    QString m_name;
};
void require(bool condition, const QString &message);
void queryExec(QSqlQuery &query, const QString &sql = {});
void queryPrepare(QSqlQuery &query, const QString &sql);
void execute(QSqlDatabase &db, const QString &sql);
void validateDatabase(QSqlDatabase &db, bool integrity);
// Consistent SQLite snapshot, including committed WAL content. Does not copy a live .sqlite file.
void snapshotDatabase(QSqlDatabase &db, const QString &newFile);
} // namespace aster::database_detail
#endif
