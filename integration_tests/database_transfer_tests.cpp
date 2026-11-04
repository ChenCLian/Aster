#include "integration_suites.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QCryptographicHash>
#include <QToolButton>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QListWidget>
#include <QAbstractButton>
#include <QLineEdit>
#include <stdexcept>
#include "io/database/database_transfer.h"
#include "io/database/result_repository.h"
#include "io/database/data_codec.h"
#include "ui/workflow/workflow_document.h"
#include "ui/results/record_directory_panel.h"
#include "ui/services/record_directory_service.h"
#include "ui/common/presentation.h"
#include "processing/scientific_registry.h"
using namespace aster;
using namespace aster::ui;
namespace
{
RunResult sample(QString label = "sample")
{
    WorkflowModel model(makeScientificRegistry());
    auto root = model.addRoot("sine_source", {0, 0}, {{"count", 16}}).nodeId;
    return WorkflowExecutor().run({model.freeze().snapshot, root, {label, {}, {}}});
}
QByteArray bytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
bool sql(const QString &path, const QString &statement)
{
    const auto name = QUuid::createUuid().toString();
    bool ok;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        ok = db.open();
        QSqlQuery q(db);
        ok = ok && q.exec(statement);
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}
} // namespace
class DatabaseTransferTests : public QObject
{
    Q_OBJECT
  private slots:
    void mergeRemapsIdsOutputsReferencesAndPeaks()
    {
        QTemporaryDir dir;
        const auto from = dir.filePath("来源 'quoted'.sqlite"), to = dir.filePath("目标.sqlite");
        ResultRepository source, target;
        QVERIFY(source.open(from).isEmpty());
        QVERIFY(target.open(to).isEmpty());
        const auto already = target.save(sample("already"));
        QVERIFY(already.saved());
        auto run = sample("spectral sample");
        SpectrumData spectrum;
        spectrum.frequency = {0, .1, .2, .3};
        spectrum.power = {1, 4, 2, 3};
        spectrum.frequencyAxis = {"Frequency", "1/ka"};
        TableData peaks;
        peaks.columns = {"frequency", "power"};
        peaks.rows = {{.1, 4}, {.3, 3}};
        run.nodes[run.root].outputs.insert("spectrum", makeData(spectrum));
        run.nodes[run.root].outputs.insert("peaks", makeData(peaks));
        auto original = source.save(run);
        QVERIFY(original.saved());
        OutputReference ref{from, original.recordId, run.runId, run.root, "series", "Series"};
        WorkflowDocument document(makeScientificRegistry());
        QVERIFY(document.addDatabaseSource(ref).ok());
        const auto derived = WorkflowExecutor().run(document.plan().requests.first());
        const auto child = source.save(derived);
        QVERIFY(child.saved());
        QVERIFY(source.updateMetadata(original.recordId, "named source", "notes").isEmpty());
        const auto before = bytes(from);
        const auto result = mergeDatabase(to, from);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.imported, 2);
        QCOMPARE(result.duplicates, 0);
        QVERIFY(QFileInfo::exists(result.backupPath));
        QCOMPARE(bytes(from), before);
        QCOMPARE(target.list().records.size(), 3);
        QCOMPARE(result.recordIds.value(original.recordId), 2);
        const auto imported = target.load(result.recordIds.value(child.recordId));
        QVERIFY2(imported.ok(), qPrintable(imported.error));
        const auto relocated =
            OutputReference::fromJson(imported.run.provenance["database_source"].toObject());
        QCOMPARE(relocated.databasePath, QFileInfo(to).canonicalFilePath());
        QCOMPARE(relocated.recordId, 2);
        QCOMPARE(relocated.runId, ref.runId);
        QCOMPARE(relocated.nodeId, ref.nodeId);
        QVERIFY(target.loadOutput(relocated).ok());
        const auto reportRef = OutputReference::fromJson(
            imported.run.nodes[derived.root].report["database_source"].toObject());
        QCOMPARE(reportRef.databasePath, relocated.databasePath);
        QCOMPARE(reportRef.recordId, relocated.recordId);
        QCOMPARE(encodeData(target.load(2).run.nodes[run.root].outputs["peaks"]).bytes,
                 encodeData(run.nodes[run.root].outputs["peaks"]).bytes);
        QCOMPARE(target.load(2).displayName, QString("named source"));
        RecordFilter filter;
        filter.minFrequency = .09;
        filter.maxFrequency = .11;
        filter.frequencyUnit = "1/ka";
        QCOMPARE(target.list(filter).records.size(), 1);
        QCOMPARE(target.list(filter).records.first().id, 2);
        const auto removed = target.remove(RecordIdentities{{2, run.runId}});
        QVERIFY(!removed.ok());
        QCOMPARE(removed.blockingOwner, result.recordIds.value(child.recordId));
        ResultRepository backup;
        QVERIFY(backup.open(result.backupPath, false).isEmpty());
        QCOMPARE(backup.list().records.size(), 1);
        // Repeated merge compares location-independent content and keeps local metadata.
        QVERIFY(target.updateMetadata(2, "local name", "local notes").isEmpty());
        const auto repeated = mergeDatabase(to, from);
        QVERIFY2(repeated.ok(), qPrintable(repeated.error));
        QCOMPARE(repeated.imported, 0);
        QCOMPARE(repeated.duplicates, 2);
        QVERIFY(repeated.backupPath.isEmpty());
        QCOMPARE(target.load(2).displayName, QString("local name"));
        QVERIFY(source
                    .remove(RecordIdentities{{original.recordId, run.runId},
                                             {child.recordId, derived.runId}})
                    .ok());
        QVERIFY(target.loadOutput(relocated)
                    .ok()); // Merged records are independent of the source file.
    }
    void cancellationRollsBackAllTables()
    {
        QTemporaryDir dir;
        auto from = dir.filePath("from.db"), to = dir.filePath("to.db");
        ResultRepository source, target;
        QVERIFY(source.open(from).isEmpty());
        QVERIFY(target.open(to).isEmpty());
        auto original = target.save(sample());
        QVERIFY(original.saved());
        for (int i = 0; i < 3; ++i)
            QVERIFY(source.save(sample()).saved());
        CancellationToken cancel;
        auto result = mergeDatabase(to, from, cancel,
                                    [&](QString stage, qint64 done, qint64)
                                    {
                                        if (stage == "writing" && done == 1)
                                            cancel.cancel();
                                    });
        QVERIFY(result.cancelled);
        QCOMPARE(result.imported, 0);
        QVERIFY(result.recordIds.isEmpty());
        QCOMPARE(target.list().records.size(), 1);
        QVERIFY(target.load(original.recordId).ok());
        QVERIFY(QFileInfo::exists(result.backupPath));
        auto retry = mergeDatabase(to, from);
        QVERIFY2(retry.ok(), qPrintable(retry.error));
        QCOMPARE(retry.imported, 3);
        QCOMPARE(retry.recordIds.first(), 2); // Rolled-back AUTOINCREMENT IDs are not committed.
    }
    void invalidDatabases_data()
    {
        QTest::addColumn<QString>("mutation");
        QTest::newRow("future-schema") << QString("UPDATE aster_meta SET schema_version=999");
        QTest::newRow("legacy-schema") << QString("UPDATE aster_meta SET schema_version=2");
        QTest::newRow("extra-version") << QString("INSERT INTO aster_meta VALUES(3)");
        QTest::newRow("trigger") << QString("CREATE TRIGGER unexpected AFTER INSERT ON aster_runs "
                                            "BEGIN DELETE FROM aster_runs; END");
        QTest::newRow("orphan") << QString("INSERT INTO aster_dependencies VALUES(123,456)");
        QTest::newRow("payload") << QString("UPDATE aster_outputs SET data=x'010203'");
        QTest::newRow("incomplete-columns")
            << QString("ALTER TABLE aster_runs RENAME COLUMN notes TO missing_notes");
        QTest::newRow("uuid") << QString("UPDATE aster_runs SET run_uuid='wrong'");
        QTest::newRow("peak-index")
            << QString("INSERT INTO aster_peaks SELECT run_id,node_uuid,'series',0,'1/ka',.1 FROM "
                       "aster_nodes LIMIT 1");
    }
    void invalidDatabases()
    {
        QFETCH(QString, mutation);
        QTemporaryDir dir;
        auto from = dir.filePath("from.db"), to = dir.filePath("to.db");
        {
            ResultRepository source, target;
            QVERIFY(source.open(from).isEmpty());
            QVERIFY(target.open(to).isEmpty());
            QVERIFY(source.save(sample()).saved());
            QVERIFY(target.save(sample()).saved());
        }
        QVERIFY(sql(from, mutation));
        const auto before = bytes(from), destination = bytes(to);
        auto result = mergeDatabase(to, from);
        QVERIFY2(!result.ok(), qPrintable(mutation));
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(result.imported, 0);
        QCOMPARE(bytes(from), before);
        QCOMPARE(bytes(to), destination);
    }
    void collisionRejectsEntireMerge()
    {
        QTemporaryDir dir;
        auto from = dir.filePath("from.db"), to = dir.filePath("to.db");
        ResultRepository source, target;
        QVERIFY(source.open(from).isEmpty());
        QVERIFY(target.open(to).isEmpty());
        const auto run = sample();
        QVERIFY(source.save(run).saved());
        QVERIFY(target.save(run).saved());
        QVERIFY(source.save(sample()).saved());
        QVERIFY(sql(from, "UPDATE aster_nodes SET error='changed' WHERE run_id=1"));
        auto result = mergeDatabase(to, from);
        QVERIFY(!result.ok());
        QVERIFY(result.error.contains("Conflicting"));
        QCOMPARE(target.list().records.size(), 1);
    }
    void walBackupContainsCommittedChanges()
    {
        QTemporaryDir dir;
        auto source = dir.filePath("active.sqlite"), copy = dir.filePath("backup.sqlite");
        ResultRepository repo;
        QVERIFY(repo.open(source).isEmpty());
        const auto name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(source);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("PRAGMA journal_mode=WAL"));
            q.finish();
            QVERIFY(q.exec("PRAGMA wal_autocheckpoint=0"));
            q.finish();
            const auto receipt = repo.save(sample("committed WAL"));
            QVERIFY(receipt.saved());
            QVERIFY(QFileInfo::exists(source + "-wal"));
            const auto result = backupDatabase(source, copy);
            QVERIFY2(result.ok(), qPrintable(result.error));
            ResultRepository backup;
            QVERIFY(backup.open(copy, false).isEmpty());
            QCOMPARE(backup.list().records.size(), 1);
            QVERIFY(backup.load(receipt.recordId).ok());
            const auto before = bytes(copy);
            QVERIFY(!backupDatabase(source, copy).ok());
            QCOMPARE(bytes(copy), before);
            QVERIFY(!mergeDatabase(source, source).ok());
        }
        QSqlDatabase::removeDatabase(name);
    }
    void externalSourcesAreRetained()
    {
        QTemporaryDir dir;
        auto ext = dir.filePath("external.db"), from = dir.filePath("from.db"),
             to = dir.filePath("to.db");
        ResultRepository external, source, target;
        QVERIFY(external.open(ext).isEmpty());
        QVERIFY(source.open(from).isEmpty());
        QVERIFY(target.open(to).isEmpty());
        const auto run = sample();
        auto receipt = external.save(run);
        QVERIFY(receipt.saved());
        OutputReference ref{ext, receipt.recordId, run.runId, run.root, "series", "Series"};
        WorkflowDocument document(makeScientificRegistry());
        QVERIFY(document.addDatabaseSource(ref).ok());
        auto child = source.save(WorkflowExecutor().run(document.plan().requests.first()));
        QVERIFY(child.saved());
        auto result = mergeDatabase(to, from);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.externalReferences, 1);
        auto loaded = target.load(result.recordIds.value(child.recordId));
        QVERIFY(loaded.ok());
        QCOMPARE(OutputReference::fromJson(loaded.run.provenance["database_source"].toObject())
                     .databasePath,
                 ext);
    }
    void failedSourceRecordsRemainInspectable()
    {
        QTemporaryDir dir;
        const auto from = dir.filePath("from.db"), to = dir.filePath("to.db");
        ResultRepository source, target;
        QVERIFY(source.open(from).isEmpty());
        QVERIFY(target.open(to).isEmpty());
        const auto run = sample();
        const auto original = source.save(run);
        QVERIFY(original.saved());
        WorkflowDocument document(makeScientificRegistry());
        OutputReference ref{from, original.recordId, run.runId, run.root, "missing_port", "Series"};
        QVERIFY(document.addDatabaseSource(ref).ok());
        const auto failed = WorkflowExecutor().run(document.plan().requests.first());
        QVERIFY(failed.state != RunState::Succeeded);
        const auto receipt = source.save(failed);
        QVERIFY(receipt.saved());
        const auto result = mergeDatabase(to, from);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.imported, 2);
        const auto loaded = target.load(result.recordIds.value(receipt.recordId));
        QVERIFY(loaded.ok());
        QCOMPARE(loaded.run.state, failed.state);
        QCOMPARE(loaded.run.nodes[failed.root].error, failed.nodes[failed.root].error);
    }
    void capturedSourceMismatchRollsBack()
    {
        QTemporaryDir dir;
        const auto from = dir.filePath("from.db"), to = dir.filePath("to.db");
        ResultRepository source, target;
        QVERIFY(source.open(from).isEmpty());
        QVERIFY(target.open(to).isEmpty());
        const auto run = sample();
        const auto original = source.save(run);
        QVERIFY(original.saved());
        WorkflowDocument document(makeScientificRegistry());
        OutputReference ref{from, original.recordId, run.runId, run.root, "series", "Series"};
        QVERIFY(document.addDatabaseSource(ref).ok());
        auto derived = WorkflowExecutor().run(document.plan().requests.first());
        SeriesData wrong;
        wrong.x = {0, 1};
        wrong.y = {99, 99};
        derived.nodes[derived.root].outputs["data"] = makeData(wrong);
        QVERIFY(source.save(derived).saved());
        const auto result = mergeDatabase(to, from);
        QVERIFY(!result.ok());
        QVERIFY(result.error.contains("captured"));
        QCOMPARE(result.imported, 0);
        QVERIFY(result.recordIds.isEmpty());
        QCOMPARE(target.list().records.size(), 0);
        QVERIFY(QFileInfo::exists(result.backupPath));
    }
    void laterSourceMergeRepairsMatchingOwner()
    {
        QTemporaryDir dir;
        const auto from = dir.filePath("all.db"), to = dir.filePath("partial.db");
        ResultRepository source, target;
        QVERIFY(source.open(from).isEmpty());
        QVERIFY(target.open(to).isEmpty());
        const auto run = sample();
        const auto original = source.save(run);
        QVERIFY(original.saved());
        WorkflowDocument document(makeScientificRegistry());
        OutputReference ref{from, original.recordId, run.runId, run.root, "series", "Series"};
        QVERIFY(document.addDatabaseSource(ref).ok());
        const auto derived = WorkflowExecutor().run(document.plan().requests.first());
        QVERIFY(source.save(derived).saved());
        const auto existing = target.save(derived);
        QVERIFY(existing.saved());
        const auto result = mergeDatabase(to, from);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.imported, 1);
        QCOMPARE(result.duplicates, 1);
        const auto updated = target.load(existing.recordId);
        QVERIFY(updated.ok());
        const auto local =
            OutputReference::fromJson(updated.run.provenance["database_source"].toObject());
        QCOMPARE(local.databasePath, QFileInfo(to).canonicalFilePath());
        QCOMPARE(local.recordId, result.recordIds.value(original.recordId));
        const auto deletion = target.remove(RecordIdentities{{local.recordId, run.runId}});
        QVERIFY(!deletion.ok());
        QCOMPARE(deletion.blockingOwner, existing.recordId);
    }
    void observerExceptionDoesNotMisreportCommit()
    {
        QTemporaryDir dir;
        const auto from = dir.filePath("from.db"), to = dir.filePath("to.db");
        ResultRepository source, target;
        QVERIFY(source.open(from).isEmpty());
        QVERIFY(target.open(to).isEmpty());
        QVERIFY(source.save(sample()).saved());
        const auto result = mergeDatabase(to, from, {},
                                          [](QString stage, qint64, qint64)
                                          {
                                              if (stage == "complete")
                                                  throw std::runtime_error("observer");
                                          });
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.imported, 1);
        QCOMPARE(target.list().records.size(), 1);
    }
    void mergeMenuRefreshesAndKeepsChecks()
    {
        QTemporaryDir dir;
        const auto from = dir.filePath("from.db"), to = dir.filePath("to.db");
        {
            ResultRepository source, target;
            QVERIFY(source.open(from).isEmpty());
            QVERIFY(target.open(to).isEmpty());
            QVERIFY(source.save(sample("incoming")).saved());
            QVERIFY(target.save(sample("existing")).saved());
        }
        RecordDirectoryPanel panel;
        panel.setDatabase(to);
        panel.show();
        auto *button = panel.findChild<QToolButton *>("databaseOperations");
        auto *list = panel.findChild<QListWidget *>("resultRecords");
        QVERIFY(button);
        QVERIFY(list);
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled() && list->count() == 1, 5000);
        list->item(0)->setCheckState(Qt::Checked);
        QCOMPARE(panel.checkedRecords().size(), 1);
        bool accepted = false, informed = false;
        const bool previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        QTimer dialogs;
        int ticks = 0;
        connect(
            &dialogs, &QTimer::timeout, this,
            [&]
            {
                ++ticks;

                if (auto *file = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
                {
                    if (!file->property("testPathSet").toBool())
                    {
                        file->setDirectory(QFileInfo(from).absolutePath());
                        file->selectFile(QFileInfo(from).fileName());
                        file->setProperty("testPathSet", true);
                    }
                    if (auto *name = file->findChild<QLineEdit *>("fileNameEdit"))
                        name->setText(from);
                    QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
                }
                else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                {
                    if (box->standardButtons().testFlag(QMessageBox::Yes))
                    {
                        accepted = true;
                        box->button(QMessageBox::Yes)->click();
                    }
                    else
                    {
                        informed = true;
                        box->button(QMessageBox::Ok)->click();
                    }
                }
                if (ticks > 250)
                {
                    if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                        dialog->reject();
                    dialogs.stop();
                }
            });
        dialogs.start(20);
        panel.findChild<QAction *>("mergeDatabaseAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(informed && button->isEnabled(), 10000);
        dialogs.stop();
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
        QVERIFY(accepted);
        QCOMPARE(panel.recordCount(), 2);
        QCOMPARE(panel.checkedRecords().size(), 1);
        QCOMPARE(panel.checkedRecords().firstKey(), 1);
    }
    void serviceSignalsAndDirectoryEntry()
    {
        setLanguage("en");
        QTemporaryDir dir;
        auto from = dir.filePath("from.db"), to = dir.filePath("to.db");
        {
            ResultRepository source, target;
            QVERIFY(source.open(from).isEmpty());
            QVERIFY(target.open(to).isEmpty());
            QVERIFY(source.save(sample()).saved());
        }
        RecordDirectoryService service;
        bool finished = false;
        DatabaseTransferResult result;
        QSignalSpy busy(&service, &RecordDirectoryService::mutationBusyChanged);
        QSignalSpy progress(&service, &RecordDirectoryService::transferProgress);
        connect(&service, &RecordDirectoryService::transferred, this,
                [&](QString path, DatabaseTransferResult value, bool merge)
                {
                    QCOMPARE(path, to);
                    QVERIFY(merge);
                    result = value;
                    finished = true;
                });
        service.merge(to, from);
        QCOMPARE(busy.size(), 1);
        QVERIFY(busy.first().first().toBool());
        QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.imported, 1);
        QCOMPARE(busy.size(), 2);
        QVERIFY(!busy.last().first().toBool());
        QVERIFY(!progress.isEmpty());
        RecordDirectoryPanel panel;
        panel.setDatabase(to);
        panel.show();
        auto *button = panel.findChild<QToolButton *>("databaseOperations");
        QVERIFY(button);
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000);
        QVERIFY(panel.findChild<QAction *>("mergeDatabaseAction"));
        QVERIFY(panel.findChild<QAction *>("backupDatabaseAction"));
        panel.setMutationsEnabled(false);
        QVERIFY(!button->isEnabled());
        panel.setMutationsEnabled(true);
        QVERIFY(button->isEnabled());
    }
};
int runDatabaseTransferTests(int argc, char **argv)
{
    DatabaseTransferTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "database_transfer_tests.moc"
