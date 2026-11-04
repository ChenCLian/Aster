#include "integration_suites.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QListWidget>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QLabel>
#include "io/database/result_repository.h"
#include "ui/results/record_directory_panel.h"
#include "ui/results/record_item_delegate.h"
#include "ui/results/results_page.h"
#include "ui/services/record_directory_service.h"
#include "ui/workflow/workflow_document.h"
#include "ui/workflow/workflow_canvas.h"
#include "ui/app/main_window.h"
#include "ui/common/presentation.h"
#include "ui/settings/theme_manager.h"
#include "processing/scientific_registry.h"
using namespace aster;
using namespace aster::ui;
namespace
{
RunResult sample(QString name = "record")
{
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("sine_source", {0, 0}, {{"count", 8}}).nodeId;
    return WorkflowExecutor().run({model.freeze().snapshot, root, {name, {}, {}}});
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
void answer(QMessageBox::StandardButton button)
{
    QTimer::singleShot(0, qApp,
                       [button]
                       {
                           if (auto *box =
                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                               box->button(button)->click();
                       });
}
} // namespace
class RecordDirectoryTests : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        setLanguage("en");
        applyTheme(*qApp);
    }
    void stablePagesAndSelectionSnapshot()
    {
        QTemporaryDir dir;
        ResultRepository repo;
        const auto path = dir.filePath("records.sqlite");
        QVERIFY(repo.open(path).isEmpty());
        for (int i = 0; i < 205; ++i)
            QVERIFY(repo.save(sample(i % 2 ? "keep" : "discard")).saved());
        auto first = repo.listBefore({}, 100);
        QCOMPARE(first.records.size(), 100);
        QVERIFY(first.hasMore);
        const auto all = repo.identities({});
        QCOMPARE(all.existing.size(), 205);
        const auto extra = repo.save(sample("new"));
        QVERIFY(extra.saved());
        auto second = repo.listBefore({}, 100, first.records.last().id);
        auto third = repo.listBefore({}, 100, second.records.last().id);
        QCOMPARE(second.records.size(), 100);
        QCOMPARE(third.records.size(), 5);
        QVERIFY(!third.hasMore);
        QSet<qint64> unique;
        for (const auto &page : {first, second, third})
            for (const auto &r : page.records)
                unique.insert(r.id);
        QCOMPARE(unique.size(), 205);
        QVERIFY(!unique.contains(extra.recordId));
        RecordFilter f;
        f.text = "keep";
        const auto state = repo.selectionState(all.existing, f);
        QVERIFY(state.error.isEmpty());
        QCOMPARE(state.existing.size(), 205);
        QCOMPARE(state.matching, 102);
        // Bulk checks remain a snapshot; newly saved records are not added.
        QVERIFY(!state.existing.contains(extra.recordId));
        const auto gone = all.existing.firstKey();
        QVERIFY(repo.remove(gone).isEmpty());
        QCOMPARE(repo.selectionState(all.existing, {}).existing.size(), 204);
    }
    void dependencyProtectionAndAtomicDelete()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("records.sqlite");
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        const auto source = sample();
        const auto receipt = repo.save(source);
        QVERIFY(receipt.saved());
        OutputReference ref{path, receipt.recordId, source.runId, source.root, "series", "Series"};
        WorkflowDocument document(makeScientificRegistry());
        const auto root = document.addDatabaseSource(ref);
        QVERIFY(root.ok());
        const auto derived = WorkflowExecutor().run(document.plan().requests.first());
        const auto child = repo.save(derived);
        QVERIFY(child.saved());
        const auto spareRun = sample("spare");
        const auto spare = repo.save(spareRun);
        QVERIFY(spare.saved());
        const RecordIdentities requested{{receipt.recordId, source.runId},
                                         {spare.recordId, spareRun.runId}};
        const auto blocked = repo.remove(requested);
        QVERIFY(!blocked.ok());
        QCOMPARE(blocked.blockingOwner, child.recordId);
        QCOMPARE(repo.list().records.size(), 3);
        QVERIFY(blocked.removed.isEmpty());
        auto both = requested;
        both.insert(child.recordId, derived.runId);
        auto wrong = both;
        wrong[receipt.recordId] = QUuid::createUuid();
        QVERIFY(!repo.remove(wrong).ok());
        QCOMPARE(repo.list().records.size(), 3);
        // A database failure after an earlier delete must roll back every row and edge.
        QVERIFY(sql(path, QString("CREATE TRIGGER reject_delete BEFORE DELETE ON aster_runs WHEN "
                                  "OLD.id=%1 BEGIN SELECT RAISE(ABORT,'test failure'); END")
                              .arg(spare.recordId)));
        QVERIFY(!repo.remove(both).ok());
        QCOMPARE(repo.list().records.size(), 3);
        QCOMPARE(repo.remove(requested).blockingOwner, child.recordId);
        QVERIFY(sql(path, "DROP TRIGGER reject_delete"));
        const auto removed = repo.remove(both);
        QVERIFY2(removed.ok(), qPrintable(removed.error));
        QCOMPARE(removed.removed.size(), 3);
        QVERIFY(repo.list().records.isEmpty());
        QVERIFY(repo.frequencyUnits().isEmpty());
    }
    void checksSurviveFiltersAndCheckboxDoesNotPlot()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("records.sqlite");
        {
            ResultRepository repo;
            QVERIFY(repo.open(path).isEmpty());
            for (int i = 0; i < 105; ++i)
                QVERIFY(repo.save(sample(i % 2 ? "keep" : "discard")).saved());
        }
        RecordDirectoryPanel panel;
        panel.resize(280, 760);
        panel.show();
        panel.setDatabase(path);
        auto *select = panel.findChild<QToolButton *>("checkMatchingRecords");
        auto *list = panel.findChild<QListWidget *>("resultRecords");
        QTRY_VERIFY(select->isEnabled());
        QCOMPARE(panel.recordCount(), 100);
        QSignalSpy plot(&panel, &RecordDirectoryPanel::recordActivated);
        const auto rect = RecordItemDelegate::checkRect(list->visualItemRect(list->item(0)));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QCOMPARE(panel.checkedRecords().size(), 1);
        QCOMPARE(plot.count(), 0);
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QTest::mouseRelease(list->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QCOMPARE(plot.count(), 0);
        QCOMPARE(panel.checkedRecords().size(), 1);
        select->click();
        QTRY_COMPARE(panel.checkedRecords().size(), 105);
        QTRY_VERIFY(select->isEnabled());
        auto *search = panel.findChild<QLineEdit *>("recordSearch");
        search->setText("keep");
        QTRY_COMPARE(panel.recordCount(), 52);
        QTRY_VERIFY(select->isEnabled());
        QCOMPARE(panel.checkedRecords().size(), 105);
        QVERIFY(panel.findChild<QLabel *>("recordSelectionCount")->text().contains("53 outside"));
        select->menu()->actions().last()->trigger();
        QTRY_COMPARE(panel.checkedRecords().size(), 53);
        QTRY_VERIFY(select->isEnabled());
        search->clear();
        QTRY_COMPARE(panel.recordCount(), 100);
        QTRY_VERIFY(select->isEnabled());
        QCOMPARE(panel.checkedRecords().size(), 53);
        panel.refresh();
        QTRY_VERIFY(select->isEnabled());
        QCOMPARE(panel.checkedRecords().size(), 53);
        list->verticalScrollBar()->setValue(list->verticalScrollBar()->maximum());
        QTRY_COMPARE(panel.recordCount(), 105);
        QTRY_VERIFY(select->isEnabled());
        list->setFocus();
        answer(QMessageBox::No);
        QTest::keyClick(list, Qt::Key_Delete);
        QCOMPARE(panel.checkedRecords().size(), 53);
        QCOMPARE(panel.recordCount(), 105);
        answer(QMessageBox::Yes);
        QTest::keyClick(list, Qt::Key_Delete);
        QTRY_COMPARE(panel.recordCount(), 52);
        QTRY_VERIFY(select->isEnabled());
        QCOMPARE(panel.checkedRecords().size(), 0);
        // Text editing never routes Delete to the directory.
        search->setText("keep");
        search->setFocus();
        search->setCursorPosition(0);
        QTest::keyClick(search, Qt::Key_Delete);
        QCOMPARE(search->text(), QString("eep"));
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        QCOMPARE(repo.list().records.size(), 52);
        QTRY_VERIFY(select->isEnabled());
        select->click();
        QTRY_COMPARE(panel.checkedRecords().size(), 52);
        QTRY_VERIFY(select->isEnabled());
        panel.setDatabase(dir.filePath("other.sqlite"));
        QTRY_COMPARE(panel.recordCount(), 0);
        QVERIFY(panel.checkedRecords().isEmpty());
    }
    void deletingCurrentRecordClosesOnlyRelatedPlots()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("records.sqlite");
        const auto first = sample("first"), second = sample("second");
        qint64 firstId, secondId;
        {
            ResultRepository repo;
            QVERIFY(repo.open(path).isEmpty());
            firstId = repo.save(first).recordId;
            secondId = repo.save(second).recordId;
        }
        ResultsPage page(makeScientificRegistry());
        page.resize(1200, 800);
        page.show();
        page.setDatabase(path);
        QTRY_VERIFY(!page.databaseBusy());
        page.openRecord(firstId);
        QTRY_COMPARE(page.recordId(), firstId);
        page.openRecord(secondId);
        QTRY_COMPARE(page.recordId(), secondId);
        QTRY_VERIFY(!page.workspace()->loading());
        QCOMPARE(page.workspace()->pageCount(), 2);
        auto *list = page.findChild<QListWidget *>("resultRecords");
        list->setCurrentRow(0);
        list->setFocus();
        answer(QMessageBox::Yes);
        QTest::keyClick(list, Qt::Key_Delete);
        QTRY_COMPARE(page.recordCount(), 1);
        QTRY_VERIFY(!page.databaseBusy());
        QTRY_COMPARE(page.workspace()->pageCount(), 1);
        QTRY_COMPARE(page.recordId(), firstId);
        QCOMPARE(page.workspace()->currentReference().recordId, firstId);
        auto *select = page.findChild<QToolButton *>("checkMatchingRecords");
        select->click();
        QTRY_VERIFY(!page.databaseBusy());
        answer(QMessageBox::Yes);
        page.findChild<QPushButton *>("deleteCheckedRecords")->click();
        QTRY_COMPARE(page.recordCount(), 0);
        QTRY_VERIFY(!page.databaseBusy());
        QTRY_VERIFY(!page.workspace()->loading());
        QCOMPARE(page.workspace()->pageCount(), 0);
        QCOMPARE(page.recordId(), qint64(0));
    }
    void canvasDeleteFocusAndUndo()
    {
        QTemporaryDir dir;
        AppSettings settings;
        settings.databasePath = dir.filePath("records.sqlite");
        MainWindow window(settings, false);
        window.show();
        QTRY_VERIFY(!window.results()->databaseBusy());
        const auto root = window.document()->addRoot("sine_source").nodeId;
        QVERIFY(window.document()->addChild(root, ChildSlot::Down, "demean").ok());
        window.canvas()->setSelectedNode(root);
        window.canvas()->setFocus();
        answer(QMessageBox::No);
        QTest::keyClick(window.canvas(), Qt::Key_Delete);
        QCOMPARE(window.document()->model().definition().nodes.size(), 2);
        answer(QMessageBox::Yes);
        QTest::keyClick(window.canvas(), Qt::Key_Delete);
        QCOMPARE(window.document()->model().definition().nodes.size(), 0);
        QVERIFY(window.document()->undo().ok());
        QCOMPARE(window.document()->model().definition().nodes.size(), 2);
        window.canvas()->setSelectedNode(root);
        window.canvas()->setEditable(false);
        QSignalSpy removed(window.canvas(), &WorkflowCanvas::removeRequested);
        QTest::keyClick(window.canvas(), Qt::Key_Delete);
        QCOMPARE(removed.count(), 0);
    }
};
int runRecordDirectoryTests(int argc, char **argv)
{
    RecordDirectoryTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "record_directory_tests.moc"
