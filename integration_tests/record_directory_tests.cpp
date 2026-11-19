// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
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
#include <QContextMenuEvent>
#include <QMessageBox>
#include <QScrollBar>
#include <QLabel>
#include <QPainter>
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

        RecordDirectoryPanel panel;
        panel.resize(280, 620);
        panel.show();
        panel.setDatabase(path);
        auto *list = panel.findChild<QListWidget *>("resultRecords");
        auto *select = panel.findChild<QPushButton *>("checkAllRecords");
        QTRY_VERIFY(panel.recordCount() == 100 && select->isEnabled());
        auto *bar = list->verticalScrollBar();
        QSignalSpy busy(&panel, &RecordDirectoryPanel::busyChanged);
        bar->setValue(bar->maximum());
        QTRY_VERIFY(panel.recordCount() >= 200 && select->isEnabled());
        QCoreApplication::processEvents(); // Run the post-query check against the new scroll range.
        QCOMPARE(busy.count(), 2); // One start and one finish for this scroll.
        QCOMPARE(panel.recordCount(), 200);
        QVERIFY(bar->value() < bar->maximum());
        bar->setValue(bar->maximum());
        QTRY_COMPARE(panel.recordCount(), 205);
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
    void failedStepsMarkOnlyAffectedRows()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("errors.sqlite");
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        WorkflowModel model(makeScientificRegistry());
        const auto root = model.addRoot("sine_source", {0, 0}, {{"count", 8}}).nodeId;
        const auto left = model.addChild(root, ChildSlot::Append, "affine").nodeId;
        const auto right = model.addChild(root, ChildSlot::Append, "affine").nodeId;
        const auto base = WorkflowExecutor().run({model.freeze().snapshot, root, {"fixture", {}, {}}});
        QCOMPARE(base.state, RunState::Succeeded);
        auto leftOK = base.nodes.value(left), rightOK = base.nodes.value(right);
        auto warning = leftOK;
        warning.warnings << "test.warning";
        const auto failure = NodeResult::failure(QStringLiteral("invalid samples"));
        const auto cancel = NodeResult::cancelled();
        const auto store = [&](QString name, RunState state, NodeResult a, NodeResult b) {
            auto run = base;
            run.runId = QUuid::createUuid();
            run.sourceLabel = name;
            run.state = state;
            run.nodes[left] = a;
            run.nodes[right] = b;
            return repo.save(run);
        };
        QVERIFY(store("ok", RunState::Succeeded, leftOK, rightOK).saved());
        QVERIFY(store("warn", RunState::Succeeded, warning, rightOK).saved());
        QVERIFY(store("failed", RunState::Failed, failure, failure).saved());
        QVERIFY(store("partial", RunState::PartialFailed, failure, rightOK).saved());
        QVERIFY(store("cancel", RunState::Cancelled, cancel, cancel).saved());
        const auto mixed = store("failure then cancel", RunState::Cancelled, failure, cancel);
        QVERIFY(mixed.saved());
        QCOMPARE(repo.load(mixed.recordId).run.nodes.value(left).error, failure.error);
        const QSet<QString> errors{"failed", "partial", "failure then cancel"};
        const auto page = repo.directoryPage({}, 100, 0);
        QCOMPARE(page.totalRecords, 6);
        for (const auto &r : page.records)
            QCOMPARE(r.hasWorkflowError, errors.contains(r.sourceLabel));
        RecordDirectoryPanel panel;
        panel.resize(280, 620);
        panel.show();
        panel.setDatabase(path);
        auto *list = panel.findChild<QListWidget *>("resultRecords");
        QTRY_COMPARE(list->count(), 6);
        for (const auto *theme : {"light", "dark"}) {
            ThemeManager::instance().apply(*qApp, theme);
            const auto red = themeColor("error");
            for (int row = 0; row < list->count(); ++row) {
                const auto *item = list->item(row);
                const bool expected = errors.contains(item->text());
                QCOMPARE(item->data(RecordRole::WorkflowError).toBool(), expected);
                QCOMPARE(item->toolTip().contains(text("ui.record_workflow_error")), expected);
                QCOMPARE(!item->data(Qt::AccessibleDescriptionRole).toString().isEmpty(), expected);
                QStyleOptionViewItem option;
                option.initFrom(list);
                option.rect = QRect(0, 0, 240, 36);
                option.widget = list;
                QImage image(option.rect.size(), QImage::Format_ARGB32);
                image.fill(themeColor("surface"));
                QPainter painter(&image);
                list->itemDelegate()->paint(&painter, option, list->model()->index(row, 0));
                painter.end();
                bool marked = false;
                for (int x = image.width() - 28; x < image.width(); ++x)
                    for (int y = 0; y < image.height(); ++y) {
                        const auto c = image.pixelColor(x, y);
                        if (qAbs(c.red() - red.red()) + qAbs(c.green() - red.green()) +
                            qAbs(c.blue() - red.blue()) < 45) marked = true;
                    }
                QCOMPARE(marked, expected);
                // The status slot stays reserved, so all filenames keep their alignment.
                const int dotX = RecordItemDelegate::checkRect(option.rect).center().x();
                const QColor dot = image.pixelColor(dotX, option.rect.center().y());
                const auto runState = item->data(RecordRole::State).toString();
                QCOMPARE(dot, themeColor(expected ? "navigation" : runState == "Succeeded" ? "success" : "muted"));
                if (runState == "PartialFailed") {
                    QVERIFY(item->toolTip().contains(text("ui.record_partial_failure")));
                    QVERIFY(!item->toolTip().contains("PartialFailed"));
                }
            }
        }
        ThemeManager::instance().apply(*qApp, "light");
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
        auto *select = panel.findChild<QPushButton *>("checkMatchingRecords");
        auto *list = panel.findChild<QListWidget *>("resultRecords");
        auto *all = panel.findChild<QPushButton *>("checkAllRecords");
        QVERIFY(all && select);
        QTRY_VERIFY(select->isEnabled());
        QCOMPARE(panel.recordCount(), 100);
        QSignalSpy plot(&panel, &RecordDirectoryPanel::recordActivated);
        const auto rect = RecordItemDelegate::checkRect(list->visualItemRect(list->item(0)));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QCOMPARE(panel.checkedRecords().size(), 0); // The hidden slot opens the row as usual.
        QCOMPARE(plot.count(), 1);
        plot.clear();
        const auto contextToggle = [&](bool checked) {
            QTimer timer;
            timer.setSingleShot(true);
            QString caption;
            QStringList actions;
            connect(&timer, &QTimer::timeout, &timer, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu) return;
                for (const auto *entry : menu->actions()) actions << entry->text();
                auto *action = menu->findChild<QAction *>("toggleRecordCheck");
                if (!action) { menu->close(); return; }
                caption = action->text();
                menu->setActiveAction(action);
                QTest::keyClick(menu, Qt::Key_Return);
            });
            timer.start(40);
            const auto point = rect.center();
            QContextMenuEvent event(QContextMenuEvent::Mouse, point, list->viewport()->mapToGlobal(point));
            QApplication::sendEvent(list->viewport(), &event);
            QCOMPARE(caption, text(checked ? "ui.uncheck_this_record" : "ui.check_this_record"));
            QCOMPARE(actions.size(), 2);
            QVERIFY(actions.contains(text("ui.edit_name_and_notes")));
            QCOMPARE(plot.count(), 0);
        };
        contextToggle(false);
        QCOMPARE(panel.checkedRecords().size(), 1);
        contextToggle(true);
        QVERIFY(panel.checkedRecords().isEmpty());
        list->setFocus();
        QTest::keyClick(list, Qt::Key_Space);
        QCOMPARE(panel.checkedRecords().size(), 1);
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QTest::mouseRelease(list->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
        QCOMPARE(plot.count(), 0);
        QCOMPARE(panel.checkedRecords().size(), 1);
        list->verticalScrollBar()->setValue(list->verticalScrollBar()->maximum());
        QTRY_COMPARE(panel.recordCount(), 105);
        QTRY_VERIFY(all->isEnabled());
        list->setCurrentRow(103);
        const QPersistentModelIndex reading = list->currentIndex();
        const auto scroll = list->verticalScrollBar()->value();
        all->click();
        QTRY_COMPARE(panel.checkedRecords().size(), 105);
        QTRY_VERIFY(select->isEnabled());
        QCOMPARE(panel.recordCount(), 105);
        QVERIFY(reading.isValid() && reading == list->currentIndex());
        QCOMPARE(list->verticalScrollBar()->value(), scroll);
        auto *search = panel.findChild<QLineEdit *>("recordSearch");
        search->setText("keep");
        QTRY_COMPARE(panel.recordCount(), 52);
        QTRY_VERIFY(select->isEnabled());
        QCOMPARE(panel.checkedRecords().size(), 105);
        QCOMPARE(panel.findChild<QLabel *>("recordSelectionCount")->text(),
                 text("ui.records_selected_of").arg(105).arg(105));
        QVERIFY(all->isChecked() && !select->isChecked());
        select->click();
        QTRY_VERIFY(select->isChecked() && select->isEnabled());
        QCOMPARE(panel.checkedRecords().size(), 105);
        select->click();
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
    void filterChangesWhileASelectionIsPending_data()
    {
        QTest::addColumn<bool>("refreshImmediately");
        QTest::newRow("debounced search") << false;
        QTest::newRow("new page already queued") << true;
    }
    void filterChangesWhileASelectionIsPending()
    {
        QFETCH(bool, refreshImmediately);
        QTemporaryDir dir;
        ResultRepository first, second;
        const auto source = dir.filePath("source.sqlite");
        const auto other = dir.filePath("other.sqlite");
        QVERIFY(first.open(source).isEmpty());
        QVERIFY(second.open(other).isEmpty());
        const auto keep = sample("first group");
        const auto keepReceipt = first.save(keep);
        QVERIFY(keepReceipt.saved());
        QVERIFY(first.save(sample("second group")).saved());
        QVERIFY(second.save(sample("second group in other database")).saved());
        RecordDirectoryPanel panel;
        panel.resize(320, 700);
        panel.show();
        panel.setDatabase(source);
        auto *list = panel.findChild<QListWidget *>("resultRecords");
        auto *search = panel.findChild<QLineEdit *>("recordSearch");
        auto *matches = panel.findChild<QPushButton *>("checkMatchingRecords");
        auto *all = panel.findChild<QPushButton *>("checkAllRecords");
        auto *remove = panel.findChild<QPushButton *>("deleteCheckedRecords");
        QVERIFY(list && search && matches && all && remove);
        search->setText("first group");
        QTRY_VERIFY(matches->isEnabled() && list->count() == 1);
        matches->click();
        search->setText("second group");
        if (refreshImmediately)
            panel.refresh();
        QTRY_VERIFY(matches->isEnabled() && list->count() == 1 &&
                    list->item(0)->text() == QStringLiteral("second group"));
        const RecordIdentities expected{{keepReceipt.recordId, keep.runId}};
        QCOMPARE(panel.checkedRecords(), expected);
        QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
        QString message;
        QTimer::singleShot(0, &panel, [&message] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                message = box->text();
                box->button(QMessageBox::No)->click();
            }
        });
        remove->click();
        QCOMPARE(message, text("ui.delete_records_confirm").arg(1).arg(1));
        QCOMPARE(first.list().records.size(), 2);
        all->click();
        panel.setDatabase(other);
        QTRY_VERIFY(all->isEnabled() && list->count() == 1 &&
                    list->item(0)->text() == QStringLiteral("second group in other database"));
        QVERIFY(panel.checkedRecords().isEmpty());
        QVERIFY(!all->isChecked() && !matches->isChecked());
    }
    void deletingCurrentRecordClosesOnlyRelatedPlots_data()
    {
        QTest::addColumn<bool>("newPage");
        QTest::newRow("new_page") << true;
        QTest::newRow("current_page") << false;
    }
    void deletingCurrentRecordClosesOnlyRelatedPlots()
    {
        QFETCH(bool, newPage);
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
        page.workspace()->setDefaultPlotNewPage(newPage);
        page.resize(1200, 800);
        page.show();
        page.setDatabase(path);
        QTRY_VERIFY(!page.databaseBusy());
        page.openRecord(firstId);
        QTRY_COMPARE(page.recordId(), firstId);
        page.selectStep(0, true);
        QTRY_VERIFY(!page.workspace()->loading());
        page.openRecord(secondId);
        QTRY_COMPARE(page.recordId(), secondId);
        page.selectStep(0, true);
        QTRY_VERIFY(!page.workspace()->loading());
        QCOMPARE(page.workspace()->pageCount(), newPage ? 2 : 1);
        QCOMPARE(page.workspace()->subplotCount(), newPage ? 1 : 2);
        auto *list = page.findChild<QListWidget *>("resultRecords");
        list->setCurrentRow(0);
        list->setFocus();
        answer(QMessageBox::Yes);
        QTest::keyClick(list, Qt::Key_Delete);
        QTRY_COMPARE(page.recordCount(), 1);
        QTRY_VERIFY(!page.databaseBusy());
        QTRY_COMPARE(page.workspace()->pageCount(), 1);
        QCOMPARE(page.workspace()->subplotCount(), 1);
        QCOMPARE(page.recordId(), qint64(0)); // Deleted browser record is cleared, unrelated figures remain.
        QCOMPARE(page.workspace()->currentReference().recordId, firstId);
        auto *select = page.findChild<QPushButton *>("checkMatchingRecords");
        QVERIFY(select);
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
    void keyboardBrowsingAndUndoRespectTheActivePage()
    {
        QTemporaryDir dir;
        AppSettings settings;
        settings.databasePath = dir.filePath("keys.sqlite");
        ResultRepository repo;
        QVERIFY(repo.open(settings.databasePath).isEmpty());
        const auto first = repo.save(sample("first"));
        const auto second = repo.save(sample("second"));
        QVERIFY(first.saved() && second.saved());
        MainWindow window(settings, false);
        window.show();
        window.activateWindow();
        const auto root = window.document()->addRoot("sine_source").nodeId;
        const auto child = window.document()->addChild(root, ChildSlot::Append, "demean").nodeId;
        QVERIFY(!child.isNull());
        window.setPage(1);
        auto *list = window.results()->findChild<QListWidget *>("resultRecords");
        QTRY_COMPARE(list->count(), 2);
        QTRY_VERIFY(!window.results()->databaseBusy());
        list->setFocus();
        QTest::keySequence(list, QKeySequence::Undo);
        QVERIFY2(window.document()->node(child), "Undo in results changed the hidden workflow");
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          list->visualItemRect(list->item(0)).center());
        QTRY_COMPARE(window.results()->recordId(), second.recordId);
        QTest::keyClick(list, Qt::Key_Down);
        QCOMPARE(list->currentRow(), 1);
        QTest::keyClick(list, Qt::Key_Space);
        QCOMPARE(list->currentItem()->checkState(), Qt::Checked);
        QCOMPARE(window.results()->recordId(), second.recordId);
        QTest::keyClick(list, Qt::Key_Return);
        QTRY_COMPARE(window.results()->recordId(), first.recordId);
        window.setPage(0);
        window.canvas()->setFocus();
        QTest::keySequence(window.canvas(), QKeySequence::Undo);
        QTRY_VERIFY(!window.document()->node(child));
        window.setPage(1);
        list->setFocus();
        QTest::keySequence(list, QKeySequence::Redo);
        QVERIFY(!window.document()->node(child));
        window.setPage(0);
        window.canvas()->setFocus();
        QTest::keySequence(window.canvas(), QKeySequence::Redo);
        QTRY_VERIFY(window.document()->node(child));
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
