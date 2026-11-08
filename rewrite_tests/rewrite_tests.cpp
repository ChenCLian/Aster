// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsItem>
#include <QContextMenuEvent>
#include <QPointer>
#include "core/workflow_rewrite.h"
#include "core/workflow_executor.h"
#include "model/workflow_codec.h"
#include "processing/builtin_tasks.h"
#include "processing/scientific_registry.h"
#include "ui/workflow/node_edit_dialog.h"
#include "ui/workflow/workflow_canvas.h"
#include "ui/services/workflow_preview_controller.h"
#include "ui/common/presentation.h"
#include "ui/settings/theme_manager.h"
#include "ui/app/main_window.h"
using namespace aster;
using namespace aster::ui;
namespace {
SourceItem sample() { return {"sample", makeData(SeriesData{{0,1,2,3}, {1,3,5,7}, {"age","ka"}, {"proxy","u"}}), {}}; }
struct Fixture {
    WorkflowDocument doc{makeScientificRegistry()};
    NodeId root, target, child, branch;
    Fixture(ChildSlot slot = ChildSlot::Down) {
        root = doc.addRoot("input_series").nodeId;
        target = doc.addChild(root, slot, "affine", {{"factor",2},{"offset",10}}).nodeId;
        child = doc.addChild(target, slot, "affine", {{"factor",3}}).nodeId;
        branch = doc.addChild(root, slot == ChildSlot::Right ? ChildSlot::Left : ChildSlot::Right, "series_summary").nodeId;
        if (!doc.setSources(root, {sample()}).isEmpty()) qFatal("fixture source failed");
    }
    QJsonObject json() const { return workflowToJson(doc.model().definition()); }
    NodeRewriteRequest request(NodeRewriteKind kind, QString method = "linear_detrend") const {
        return {kind, target, method, "series", {{kind == NodeRewriteKind::Replace ? child : target, "trend"}}, {}};
    }
};
QAction* findAction(QMenu* menu, const QString& key) {
    for (auto* a : menu->actions()) { if (a->data().toString() == key) return a;
        if (a->menu()) if (auto* r = findAction(a->menu(), key)) return r; }
    return nullptr;
}
QVector<double> output(const RunResult& run, NodeId id) {
    const auto node = run.nodes.value(id); const auto* data = dataAs<SeriesData>(node.outputs.value("series"));
    return data ? data->y : QVector<double>{};
}
bool drop(WorkflowCanvas& canvas, QPoint point, QString key) {
    QMimeData mime; mime.setData(TaskMimeType,key.toUtf8());
    QDragEnterEvent enter(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(),&enter);
    QDragMoveEvent move(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(),&move);
    QDropEvent event(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(),&event); return event.isAccepted();
}
}
class RewriteTests: public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { setLanguage("zh"); applyTheme(*qApp); qRegisterMetaType<NodeId>("aster::NodeId"); }
    void insertSlots_data() { QTest::addColumn<int>("slot"); for(int i=0;i<3;++i) QTest::newRow(qPrintable(QString::number(i))) << i; }
    void insertSlots() {
        QFETCH(int,slot); Fixture f{ChildSlot(slot)}; const auto before=f.json();
        const auto old=f.doc.model().definition(); const auto source=f.doc.sources().value(f.root).first().data;
        const auto frozen=f.doc.plan().requests.first(); const auto rev=f.doc.revision();
        const auto prepared=f.doc.prepareRewrite(f.request(NodeRewriteKind::InsertBefore));
        QVERIFY2(prepared.ok(),qPrintable(prepared.error)); QCOMPARE(f.json(),before); QCOMPARE(f.doc.revision(),rev);
        QCOMPARE(prepared.shiftedNodes,2); QVERIFY(f.doc.commitEdit(prepared.edit).ok());
        QCOMPARE(f.doc.node(f.target)->input->parent,prepared.nodeId); QCOMPARE(f.doc.node(f.target)->input->output,QString("trend"));
        QCOMPARE(f.doc.node(f.child)->input->parent,f.target); QCOMPARE(f.doc.sources().value(f.root).first().data,source);
        QCOMPARE(f.doc.model().definition().layout.value(f.branch),old.layout.value(f.branch));
        QCOMPARE(f.doc.model().definition().nodes.size(),5);
        const auto run=WorkflowExecutor().run(f.doc.plan().requests.first()); QCOMPARE(run.state,RunState::Succeeded);
        const auto ys=output(run,f.child); QCOMPARE(ys.size(),4); QVERIFY(std::abs(ys[0]-36)<1e-9); QVERIFY(std::abs(ys[3]-72)<1e-9);
        QCOMPARE(WorkflowExecutor().run(frozen).nodes.size(),4);
        const auto after=f.json(); QVERIFY(f.doc.undo().ok()); QCOMPARE(f.json(),before);
        QVERIFY(f.doc.redo().ok()); QCOMPARE(f.json(),after);
    }
    void replaceMultipleOutputs() {
        Fixture f; const auto right=f.doc.addChild(f.target,ChildSlot::Right,"affine").nodeId; QVERIFY(!right.isNull());
        const auto before=f.json(); const auto layout=f.doc.model().definition().layout;
        auto request=f.request(NodeRewriteKind::Replace); request.childOutputs[right]="detrended";
        const auto p=f.doc.prepareRewrite(request); QVERIFY2(p.ok(),qPrintable(p.error)); QVERIFY(f.doc.commitEdit(p.edit).ok());
        QCOMPARE(p.nodeId,f.target); QCOMPARE(f.doc.node(f.target)->method,QString("linear_detrend"));
        QVERIFY(!f.doc.node(f.target)->parameters.contains("factor")); QVERIFY(!f.doc.node(f.target)->parameters.contains("offset"));
        QCOMPARE(f.doc.model().definition().layout,layout); QCOMPARE(f.doc.node(right)->input->output,QString("detrended"));
        auto run=WorkflowExecutor().run(f.doc.plan().requests.first()); QCOMPARE(run.state,RunState::Succeeded);
        QVERIFY(std::abs(output(run,f.child)[0]-3)<1e-9); QVERIFY(std::abs(output(run,right)[3])<1e-9);
        QVERIFY(f.doc.undo().ok()); QCOMPARE(f.json(),before); QVERIFY(f.doc.redo().ok());
        QCOMPARE(f.doc.node(f.child)->input->output,QString("trend"));
    }
    void replaceLeafChangesOutputKind() {
        Fixture f; NodeRewriteRequest r{NodeRewriteKind::Replace,f.child,"series_summary","series",{}, {}};
        const auto p=f.doc.prepareRewrite(r); QVERIFY2(p.ok(),qPrintable(p.error)); QVERIFY(f.doc.commitEdit(p.edit).ok());
        QCOMPARE(WorkflowExecutor().run(f.doc.plan().requests.first()).state,RunState::Succeeded);
    }
    void rejectsInvalidRequests_data() {
        QTest::addColumn<int>("which"); for(int i=0;i<9;++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void rejectsInvalidRequests() {
        QFETCH(int,which); Fixture f; const auto before=f.json(); const auto rev=f.doc.revision();
        auto r=f.request(NodeRewriteKind::Replace);
        switch(which) {
        case 0:r.method="unknown";break; case 1:r.method="sine_source";break;
        case 2:r.method="affine";break; case 3:r.method="series_summary";break;
        case 4:r.inputOutput="missing";break; case 5:r.childOutputs.clear();break;
        case 6:r.childOutputs[f.branch]="trend";break; case 7:r.childOutputs[f.child]="missing";break;
        case 8:r.target=f.root;break;
        }
        QVERIFY(!f.doc.prepareRewrite(r).ok()); QCOMPARE(f.json(),before); QCOMPARE(f.doc.revision(),rev);
    }
    void staleLockedAndCancel() {
        Fixture f; auto r=f.request(NodeRewriteKind::InsertBefore); const auto before=f.json();
        auto p=f.doc.prepareRewrite(r); QVERIFY(p.ok()); QCOMPARE(f.json(),before);
        f.doc.setEditingLocked(true); QVERIFY(!f.doc.commitEdit(p.edit).ok()); QVERIFY(!f.doc.prepareRewrite(r).ok());
        f.doc.setEditingLocked(false); QVERIFY(!f.doc.commitEdit(p.edit).ok()); QCOMPARE(f.json(),before);
        p=f.doc.prepareRewrite(r); QVERIFY(f.doc.setParameters(f.target,{{"factor",4}}).ok());
        QVERIFY(!f.doc.commitEdit(p.edit).ok()); QVERIFY(f.doc.undo().ok()); QCOMPARE(f.json(),before);
        QVERIFY(!f.doc.commitEdit(p.edit).ok()); p=f.doc.prepareRewrite(r); QVERIFY(f.doc.commitEdit(p.edit).ok());
        const auto after=f.json(); QVERIFY(!f.doc.commitEdit(p.edit).ok()); QCOMPARE(f.json(),after);
    }
    void layoutFallbackAndBlocked() {
        Fixture f; auto draft=f.doc.beginEdit(); const auto registry=f.doc.model().registry();
        // Unrelated roots occupy all three candidate translations of the chain.
        const auto block=[&](GridPosition position) { const auto id=QUuid::createUuid();
            draft.state.definition.nodes.push_back({id,"sine_source",registry.find("sine_source")->descriptor.version,{}, {}});
            draft.state.definition.layout[id]=position; return id; };
        const auto down=block({0,3}); QVERIFY(f.doc.commitEdit(f.doc.prepareEdit(draft).edit).ok());
        auto p=f.doc.prepareRewrite(f.request(NodeRewriteKind::InsertBefore)); QVERIFY2(p.ok(),qPrintable(p.error));
        QVERIFY(f.doc.commitEdit(p.edit).ok()); QCOMPARE(f.doc.node(f.target)->input->slot,ChildSlot::Left);
        QVERIFY(f.doc.undo().ok()); draft=f.doc.beginEdit(); block({-1,1}); block({1,1});
        QVERIFY(f.doc.commitEdit(f.doc.prepareEdit(draft).edit).ok()); const auto before=f.json();
        p=f.doc.prepareRewrite(f.request(NodeRewriteKind::InsertBefore)); QCOMPARE(p.error,QString("NODE_EDIT_LAYOUT")); QCOMPARE(f.json(),before);
        QVERIFY(f.doc.node(down));
    }
    void jointBoundaries() {
        Fixture f; const auto joint=f.doc.addChild(f.child,ChildSlot::Down,"cross_spectrum").nodeId;
        QVERIFY(!joint.isNull());
        QCOMPARE(nodeRewriteAvailability(f.doc.model(),NodeRewriteKind::Replace,f.child),QString("NODE_EDIT_JOINT"));
        QCOMPARE(nodeRewriteAvailability(f.doc.model(),NodeRewriteKind::InsertBefore,joint),QString("NODE_EDIT_JOINT"));
        QVERIFY(!inspectNodeRewrite(f.doc.model(),NodeRewriteKind::Replace,f.target,"cross_spectrum").ok());
        // Ancestors may be rewritten without changing the paired node's source identities.
        QVERIFY(f.doc.prepareRewrite(f.request(NodeRewriteKind::InsertBefore)).ok());
    }
    void largeSubtreeAndLimit() {
        WorkflowDocument doc(makeScientificRegistry()); WorkflowDefinition graph;
        const auto& reg=doc.model().registry(); NodeId parent,root,first;
        for(int i=0;i<WorkflowNodeLimit;++i) { const auto id=QUuid::createUuid(); const QString method=i?"affine":"sine_source";
            NodeDefinition n{id,method,reg.find(method)->descriptor.version,{}, {}};
            if(i) n.input=InputBinding{parent,"series",ChildSlot::Down}; else root=id;
            if(i==1) first=id;
            graph.nodes.push_back(n); graph.layout[id]={0,i}; parent=id; }
        QVERIFY(doc.replaceDefinition(graph).ok());
        NodeRewriteRequest r{NodeRewriteKind::InsertBefore,first,"linear_detrend","series",{{first,"trend"}}, {}};
        QCOMPARE(doc.prepareRewrite(r).error,QString("NODE_EDIT_LIMIT")); QVERIFY(doc.removeSubtree(parent).ok());
        QElapsedTimer clock; clock.start(); const auto p=doc.prepareRewrite(r);
        QVERIFY2(p.ok(),qPrintable(p.error)); QCOMPARE(p.shiftedNodes,WorkflowNodeLimit-2);
        qInfo() << "prepare insertion, 9998 moved nodes:" << clock.elapsed() << "ms";
        QVERIFY(doc.commitEdit(p.edit).ok()); QCOMPARE(doc.model().definition().nodes.size(),WorkflowNodeLimit);
    }
    void dialogMatrix_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme");
        for(const QString lang:{"zh","en"}) for(const QString style:{"light","dark"})
            QTest::newRow(qPrintable(lang+"-"+style)) << lang << style;
    }
    void dialogMatrix() {
        QFETCH(QString,language); QFETCH(QString,theme); setLanguage(language); ThemeManager::instance().apply(*qApp,theme);
        Fixture f; QWidget owner; owner.resize(580,500); owner.show();
        const auto before=f.json(); auto* dialog=new NodeEditDialog(&f.doc,NodeRewriteKind::Replace,f.target,"linear_detrend",&owner);
        dialog->openAt(QRect(owner.mapToGlobal(QPoint(570,480)),QSize(1,1))); QVERIFY(dialog->isVisible());
        auto* confirm=dialog->findChild<QPushButton*>("nodeEditConfirm"); QVERIFY(confirm); QVERIFY(!confirm->isEnabled());
        auto* box=dialog->findChild<QComboBox*>("nodeEditOutput_"+f.child.toString(QUuid::WithoutBraces)); QVERIFY(box);
        box->setCurrentIndex(box->findData("trend")); QVERIFY(confirm->isEnabled());
        QVERIFY(dialog->width()<=owner.width()); QVERIFY(dialog->height()<=owner.height());
        const auto screenshots=qEnvironmentVariable("ASTER_REWRITE_SCREENSHOTS");
        if(!screenshots.isEmpty()) QVERIFY(dialog->grab().save(screenshots+"/review-"+language+"-"+theme+".png"));
        QSignalSpy committed(dialog,&NodeEditDialog::committed); QTest::mouseClick(confirm,Qt::LeftButton); QCOMPARE(committed.count(),1);
        QCOMPARE(f.doc.node(f.target)->method,QString("linear_detrend")); QVERIFY(f.doc.undo().ok()); QCOMPARE(f.json(),before);
        setLanguage("zh"); ThemeManager::instance().apply(*qApp,"light");
    }
    void dialogCancelStaleAndDestroyed() {
        Fixture f; QWidget owner; owner.resize(700,600); owner.show(); const auto before=f.json();
        QPointer<NodeEditDialog> dialog=new NodeEditDialog(&f.doc,NodeRewriteKind::InsertBefore,f.target,"affine",&owner);
        dialog->openAt(QRect(owner.mapToGlobal(QPoint(50,50)),QSize(1,1))); QTest::keyClick(dialog,Qt::Key_Escape);
        QCOMPARE(f.json(),before); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); QVERIFY(!dialog);
        dialog=new NodeEditDialog(&f.doc,NodeRewriteKind::InsertBefore,f.target,"affine",&owner);
        QVERIFY(f.doc.removeSubtree(f.target).ok()); if(dialog) dialog->openAt(QRect());
        QVERIFY(!dialog || !dialog->isVisible());
        auto* document=new WorkflowDocument(makeScientificRegistry()); const auto root=document->addRoot("sine_source").nodeId;
        const auto child=document->addChild(root,ChildSlot::Down,"affine").nodeId;
        dialog=new NodeEditDialog(document,NodeRewriteKind::InsertBefore,child,"affine",&owner);
        delete document; if(dialog) dialog->openAt(QRect()); QVERIFY(!dialog || !dialog->isVisible());
    }
    void smallReviewAndLockedConfirm() {
        Fixture f; QWidget owner; owner.resize(340,300); owner.show();
        const auto before=f.json(); auto* dialog=new NodeEditDialog(&f.doc,NodeRewriteKind::InsertBefore,f.target,"affine",&owner);
        dialog->openAt(QRect(owner.mapToGlobal(QPoint(335,295)),QSize(1,1))); QVERIFY(dialog->isVisible());
        auto* confirm=dialog->findChild<QPushButton*>("nodeEditConfirm"); QVERIFY(confirm->isEnabled());
        QVERIFY(dialog->rect().contains(confirm->mapTo(dialog,confirm->rect().center())));
        QVERIFY(dialog->width()<=owner.width()); QVERIFY(dialog->height()<=owner.height());
        f.doc.setEditingLocked(true); QTest::mouseClick(confirm,Qt::LeftButton); QCOMPARE(f.json(),before);
    }
    void canvasEntryPoints() {
        Fixture f; WorkflowCanvas canvas(&f.doc); canvas.resize(1000,800); canvas.show(); canvas.fitWorkflow();
        QSignalSpy insert(&canvas,&WorkflowCanvas::insertRequested),replaceDrop(&canvas,&WorkflowCanvas::replaceMethodDropped),
                   insertDrop(&canvas,&WorkflowCanvas::insertMethodDropped),addDrop(&canvas,&WorkflowCanvas::methodDropped);
        const auto before=f.json(); QTest::mouseClick(canvas.viewport(),Qt::LeftButton,Qt::NoModifier,canvas.insertionViewportPosition(f.target));
        QCOMPARE(insert.count(),1); QCOMPARE(insert.first().first().value<NodeId>(),f.target);
        QVERIFY(drop(canvas,canvas.insertionViewportPosition(f.target),"affine")); QCOMPARE(insertDrop.count(),1);
        QVERIFY(drop(canvas,canvas.mapFromScene(canvas.nodeRect(f.target).center()),"linear_detrend")); QCOMPARE(replaceDrop.count(),1);
        QVERIFY(!drop(canvas,canvas.mapFromScene(canvas.nodeRect(f.root).center()),"linear_detrend"));
        QVERIFY(!drop(canvas,canvas.mapFromScene(canvas.nodeRect(f.target).center()),"mtm"));
        canvas.setSelectedNode(f.child); QVERIFY(drop(canvas,canvas.slotViewportPosition(ChildSlot::Down),"affine")); QCOMPARE(addDrop.count(),1);
        const auto screenshots=qEnvironmentVariable("ASTER_REWRITE_SCREENSHOTS");
        if(!screenshots.isEmpty()) QVERIFY(canvas.grab().save(screenshots+"/canvas-insert-controls.png"));
        canvas.setEditable(false); QVERIFY(!drop(canvas,canvas.mapFromScene(canvas.nodeRect(f.target).center()),"linear_detrend"));
        QCOMPARE(f.json(),before);
    }
    void previewAndExecutionAgreeAfterUndo() {
        Fixture f; WorkflowPreviewController preview(&f.doc); preview.setSource(f.root,f.doc.sources().value(f.root).first(),true);
        preview.selectNode(f.child); preview.setActive(true);
        QTRY_COMPARE_WITH_TIMEOUT(preview.state(),WorkflowPreviewController::State::Ready,10000);
        QVERIFY(std::abs(dataAs<SeriesData>(preview.result()->output.outputs.value("series"))->y[0]-36)<1e-9);
        auto p=f.doc.prepareRewrite(f.request(NodeRewriteKind::Replace)); QVERIFY(p.ok()); QVERIFY(f.doc.commitEdit(p.edit).ok());
        QTRY_COMPARE_WITH_TIMEOUT(preview.state(),WorkflowPreviewController::State::Ready,10000);
        QVERIFY(std::abs(dataAs<SeriesData>(preview.result()->output.outputs.value("series"))->y[0]-3)<1e-9);
        QCOMPARE(output(WorkflowExecutor().run(f.doc.plan().requests.first()),f.child),
                 dataAs<SeriesData>(preview.result()->output.outputs.value("series"))->y);
        QVERIFY(f.doc.undo().ok()); QTRY_COMPARE_WITH_TIMEOUT(preview.state(),WorkflowPreviewController::State::Ready,10000);
        QVERIFY(std::abs(dataAs<SeriesData>(preview.result()->output.outputs.value("series"))->y[0]-36)<1e-9);
        p=f.doc.prepareRewrite({NodeRewriteKind::InsertBefore,f.target,"affine","series",{{f.target,"series"}},{{"factor",2}}});
        QVERIFY(p.ok()); QVERIFY(f.doc.commitEdit(p.edit).ok());
        QTRY_COMPARE_WITH_TIMEOUT(preview.state(),WorkflowPreviewController::State::Ready,10000);
        QVERIFY(std::abs(dataAs<SeriesData>(preview.result()->output.outputs.value("series"))->y[0]-42)<1e-9);
    }
    void mainWindowMenuAndDrag() {
        QTemporaryDir dir; AppSettings settings; settings.databasePath=dir.filePath("rewrite.sqlite");
        MainWindow window(settings,false); window.resize(1300,850); window.show();
        auto* doc=window.document(); const auto root=doc->addRoot("sine_source").nodeId;
        const auto target=doc->addChild(root,ChildSlot::Down,"affine").nodeId; window.canvas()->fitWorkflow(); window.activateWindow(); QTest::qWait(50);
        const auto before=workflowToJson(doc->model().definition());
        QTest::mouseClick(window.canvas()->viewport(),Qt::LeftButton,Qt::NoModifier,window.canvas()->insertionViewportPosition(target));
        QMenu* menu=nullptr; for(auto* m:window.findChildren<QMenu*>("taskPicker")) if(m->isVisible()) menu=m; QVERIFY(menu);
        auto* action=findAction(menu,"affine"); QVERIFY(action); auto* leaf=qobject_cast<QMenu*>(action->parent()); QVERIFY(leaf);
        QVector<QMenu*> chain; for(auto* m=leaf;m!=menu;m=qobject_cast<QMenu*>(m->parentWidget())) { QVERIFY(m); chain.prepend(m); }
        auto* current=menu; for(auto* next:chain) { current->setActiveAction(next->menuAction()); QTest::keyClick(current,Qt::Key_Right); QTRY_VERIFY(next->isVisible()); current=next; }
        leaf->setActiveAction(action); QTest::keyClick(leaf,Qt::Key_Return);
        QPointer<NodeEditDialog> dialog=window.findChild<NodeEditDialog*>(); QVERIFY(dialog); QTRY_VERIFY(dialog && dialog->isVisible());
        QCOMPARE(workflowToJson(doc->model().definition()),before);
        QTest::mouseClick(dialog->findChild<QPushButton*>("nodeEditConfirm"),Qt::LeftButton);
        QCOMPARE(doc->model().definition().nodes.size(),3); QCOMPARE(doc->node(target)->input->parent,window.canvas()->selectedNode());
        QVERIFY(doc->undo().ok()); QCOMPARE(workflowToJson(doc->model().definition()),before);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); window.canvas()->fitWorkflow();
        QVERIFY(drop(*window.canvas(),window.canvas()->mapFromScene(window.canvas()->nodeRect(target).center()),"linear_detrend"));
        dialog=window.findChild<NodeEditDialog*>(); QVERIFY(dialog); QTRY_VERIFY(dialog && dialog->isVisible());
        QTest::mouseClick(dialog->findChild<QPushButton*>("nodeEditConfirm"),Qt::LeftButton);
        QCOMPARE(doc->node(target)->method,QString("linear_detrend")); QCOMPARE(window.canvas()->selectedNode(),target);
        QVERIFY(doc->undo().ok()); QCOMPARE(workflowToJson(doc->model().definition()),before);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        // Real context-menu activation must leave the task catalogue usable.
        const auto point=window.canvas()->mapFromScene(window.canvas()->nodeRect(target).center());
        QContextMenuEvent context(QContextMenuEvent::Mouse,point,window.canvas()->viewport()->mapToGlobal(point));
        QApplication::sendEvent(window.canvas()->viewport(),&context);
        QMenu* contextMenu=nullptr; QAction* replace=nullptr;
        for(auto* m:window.canvas()->findChildren<QMenu*>()) if(m->isVisible() && findAction(m,"replace_node")) { contextMenu=m; replace=findAction(m,"replace_node"); }
        QVERIFY(contextMenu); QVERIFY(replace->isEnabled()); contextMenu->setActiveAction(replace); QTest::keyClick(contextMenu,Qt::Key_Return);
        QMenu* replacementMenu=nullptr; for(auto* m:window.findChildren<QMenu*>("taskPicker")) if(m->isVisible()) replacementMenu=m;
        QVERIFY(replacementMenu); QVERIFY(findAction(replacementMenu,"linear_detrend")); replacementMenu->close();
        window.canvas()->insertMethodDropped("affine",target); dialog=window.findChild<NodeEditDialog*>(); QVERIFY(dialog);
        QVERIFY(doc->removeSubtree(target).ok()); QCoreApplication::processEvents(); QVERIFY(!dialog || !dialog->isVisible());
    }
};
QTEST_MAIN(RewriteTests)
#include "rewrite_tests.moc"
