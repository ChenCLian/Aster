// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QSpinBox>
#include "ui/panels/joint_input_dialog.h"
#include "ui/common/error_presenter.h"
#include "processing/cross_spectrum_task.h"
#include <QMenu>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include "ui/common/presentation.h"
#include "ui/settings/theme_manager.h"
#include "ui/results/results_page.h"
#include "ui/results/plot_workspace.h"
#include "ui/panels/plot_style_editor.h"
#include "ui/app/main_window.h"
#include "ui/panels/parameter_editor.h"
#include "ui/panels/workflow_preview_panel.h"
#include <QScrollArea>
#include "ui/panels/collapsible_panel.h"
#include "io/database/result_repository.h"
#include "processing/scientific_registry.h"
#include "third_party/qcustomplot/qcustomplot.h"
#include "../../tests/support/workspace_fixture.h"
using namespace aster;
using namespace aster::ui;
namespace {
DataRef samples(int n, int seed = 0) {
    SeriesData d; d.xAxis={"Time","ka"};d.yAxis={"Signal","u"};
    for(int i=0;i<n;++i){d.x.push_back(i*.25);d.y.push_back(std::sin(i*.15+seed)+.4*std::cos(i*.033)+seed*.1);}
    return makeData(std::move(d));
}
QList<QPushButton*> titles(QWidget* root){
 auto list=root->findChildren<QPushButton*>("selectSubplot");
 std::sort(list.begin(),list.end(),[root](auto*a,auto*b){const auto x=a->mapTo(root,QPoint()),y=b->mapTo(root,QPoint());return x.y()==y.y()?x.x()<y.x():x.y()<y.y();});return list;
}
bool waitPlots(PlotWorkspace &w){
 QElapsedTimer t;t.start();
 do {QTest::qWait(40);} while((w.loading()||t.elapsed()<260)&&t.elapsed()<10000);
 return !w.loading();
}
qint64 rssKiB(){
#ifdef Q_OS_LINUX
 QFile f("/proc/self/status");if(f.open(QIODevice::ReadOnly)){const auto s=QString::fromUtf8(f.readAll());const auto m=QRegularExpression("VmRSS:\\s+(\\d+)").match(s);if(m.hasMatch())return m.captured(1).toLongLong();}
#endif
 return -1;
}
void capture(QWidget& w,const QString& name){const auto dir=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");if(!dir.isEmpty()){QDir().mkpath(dir);if(!w.grab().save(dir+"/"+name+".png"))qFatal("capture failed");}}
}
class Acceptance:public QObject{
 Q_OBJECT
 QTemporaryDir temporary;
 QString path;
 QVector<OutputReference> refs;
 RunResult science;
 NodeId mtm,cwt;
 qint64 scientificId=0;
private slots:
 void initTestCase();
 void resultScreenMatrix_data();
 void resultScreenMatrix();
 void workflowScreenMatrix_data();
 void workflowScreenMatrix();
 void mixedPlotOperations_data();
 void mixedPlotOperations();
 void staleContextMenuDoesNotTouchNewPage();
 void parameterCatalogFits_data();
 void parameterCatalogFits();
 void denseLegendStaysInsideAxes();
 void jointSearchUsesVisibleNames();
 void shortSinePairExplainsSegmentsAndRecovers();
};
void Acceptance::initTestCase(){
 QVERIFY(temporary.isValid());path=temporary.filePath("acceptance.sqlite");ResultRepository repo;QVERIFY(repo.open(path).isEmpty());
 for(int i=0;i<6;++i){WorkflowModel m(makeScientificRegistry());auto root=m.addRoot("input_series",{0,0}).nodeId;
 auto r=WorkflowExecutor().run({m.freeze().snapshot,root,{QString("Record %1").arg(i),samples(960,i),{}}});
 QVERIFY(r.nodes[root].state==NodeState::Succeeded);auto receipt=repo.save(r);QVERIFY(receipt.saved());refs.push_back({path,receipt.recordId,r.runId,root,"series","Series","input_series"});}
 WorkflowModel m(makeScientificRegistry());auto root=m.addRoot("input_series",{0,0}).nodeId;
 mtm=m.addChild(root,ChildSlot::Left,"mtm").nodeId;cwt=m.addChild(root,ChildSlot::Right,"cwt").nodeId;
 science=WorkflowExecutor().run({m.freeze().snapshot,root,{"Long scientific record — 小波与频谱交互验收_10000以内",samples(4096),{}}});
 QVERIFY(science.nodes[mtm].state==NodeState::Succeeded);QVERIFY(science.nodes[cwt].state==NodeState::Succeeded);
 auto receipt=repo.save(science);QVERIFY(receipt.saved());scientificId=receipt.recordId;
}
void Acceptance::resultScreenMatrix_data(){
 QTest::addColumn<QString>("language");QTest::addColumn<QString>("theme");QTest::addColumn<QSize>("size");
 for(auto lang:{"zh","en"})for(auto theme:{"light","dark"})for(auto size:{QSize(1280,800),QSize(1920,1080)})
 QTest::newRow(qPrintable(QString("%1-%2-%3").arg(lang,theme).arg(size.width())))<<QString(lang)<<QString(theme)<<size;
}
void Acceptance::resultScreenMatrix(){
 QFETCH(QString,language);QFETCH(QString,theme);QFETCH(QSize,size);setLanguage(language);QVERIFY(ThemeManager::instance().apply(*qApp,theme).isEmpty());
 ResultsPage page(makeScientificRegistry());page.resize(size);page.setDatabase(path);page.show();page.openRecord(scientificId);
 QTRY_COMPARE(page.recordId(),scientificId);QTRY_VERIFY(page.plotView()->hasPlot());QVERIFY(waitPlots(*page.workspace()));page.workspace()->reset();
 OutputReference power{path,scientificId,science.runId,cwt,"power","Grid","cwt"};
 OutputReference spec{path,scientificId,science.runId,mtm,"spectrum","Spectrum","mtm"};
 page.workspace()->open(power,PlotWorkspace::Target::Subplot);QVERIFY(waitPlots(*page.workspace()));
 page.workspace()->open(spec,PlotWorkspace::Target::Subplot);QVERIFY(waitPlots(*page.workspace()));
 page.workspace()->open(refs[0],PlotWorkspace::Target::Subplot);QVERIFY(waitPlots(*page.workspace()));
 auto* host=page.workspace()->findChild<PlotLayoutWidget*>();QVERIFY(host);QCOMPARE(host->snapshot().order().size(),3);
 QVERIFY(host->snapshot().regions().value(host->snapshot().order().last()).width()>.99);
 auto* step=page.findChild<QTableWidget*>("resultSteps");QVERIFY(step);
 QTest::mouseClick(step->viewport(),Qt::LeftButton,Qt::NoModifier,step->visualItemRect(step->item(1,1)).center());QVERIFY(waitPlots(*page.workspace()));
 auto* outputs=page.findChild<QTableView*>("resultOutputs");QVERIFY(outputs);
 auto* model=qobject_cast<QStandardItemModel*>(outputs->model());QVERIFY(model);
 for(int i=0;i<model->rowCount();++i)if(model->item(i,0)->isCheckable())model->item(i,0)->setCheckState(Qt::Checked);
 auto* add=page.findChild<QPushButton*>("plotCheckedOutputs");auto* overlay=page.findChild<QPushButton*>("overlayCheckedOutputs");QVERIFY(add);QVERIFY(overlay);
 auto* destination=page.findChild<QLabel*>("plotDestination");QVERIFY(destination);
 QTest::qWait(80); // Text changes post a Qt layout request; inspect after event delivery.
 capture(page,QString("results-precheck-%1-%2-%3").arg(language,theme).arg(size.width()));
 for(auto* button:{add,overlay}){const QRect r(button->mapTo(&page,QPoint()),button->size());QVERIFY2(page.rect().contains(r),qPrintable(button->text()));QVERIFY2(button->width()>=button->fontMetrics().horizontalAdvance(button->text())+8,qPrintable(QString("%1 width=%2 text=%3").arg(button->text()).arg(button->width()).arg(button->fontMetrics().horizontalAdvance(button->text()))));}
 QCOMPARE(step->selectionModel()->selectedRows().size(),1);
 auto* inspector=page.findChild<QTabWidget*>("resultInspectorTabs");QVERIFY(inspector);
 for(int i=0;i<inspector->count();++i){QTest::mouseClick(inspector->tabBar(),Qt::LeftButton,Qt::NoModifier,inspector->tabBar()->tabRect(i).center());QCOMPARE(inspector->currentIndex(),i);}
 inspector->setCurrentIndex(0);QTest::qWait(80);
 capture(page,QString("results-%1-%2-%3").arg(language,theme).arg(size.width()));
 auto* toggle=page.findChild<QToolButton*>("toggleResultOutputs");QVERIFY(toggle);QTest::mouseClick(toggle,Qt::LeftButton);QTest::qWait(60);QTest::mouseClick(toggle,Qt::LeftButton);QTest::qWait(60);QVERIFY(add->isVisible());
 page.resize(1024,700);QTest::qWait(80);QVERIFY(waitPlots(*page.workspace()));capture(page,QString("results-narrow-%1-%2-%3").arg(language,theme).arg(size.width()));
 for(auto*view:page.workspace()->findChildren<PlotView*>())if(view->isVisible()&&view->hasPlot()){
  auto*plot=view->plotWidget();if(!plot->legend->visible()||!plot->legend->itemCount())continue;
  QVERIFY2(plot->axisRect()->rect().contains(plot->legend->outerRect()),qPrintable(QString("axes=%1,%2 %3x%4 legend=%5,%6 %7x%8 minimum=%9x%10").arg(plot->axisRect()->rect().x()).arg(plot->axisRect()->rect().y()).arg(plot->axisRect()->rect().width()).arg(plot->axisRect()->rect().height()).arg(plot->legend->outerRect().x()).arg(plot->legend->outerRect().y()).arg(plot->legend->outerRect().width()).arg(plot->legend->outerRect().height()).arg(view->minimumWidth()).arg(view->minimumHeight())));
 }
}
void Acceptance::workflowScreenMatrix_data(){resultScreenMatrix_data();}
void Acceptance::workflowScreenMatrix(){
 QFETCH(QString,language);QFETCH(QString,theme);QFETCH(QSize,size);setLanguage(language);QVERIFY(ThemeManager::instance().apply(*qApp,theme).isEmpty());
 AppSettings settings;settings.databasePath=temporary.filePath(QString("workflow-%1-%2-%3.sqlite").arg(language,theme).arg(size.width()));settings.language=language;settings.theme=theme;
 MainWindow window(settings,false);window.resize(size);window.show();auto fixture=testsupport::installWorkspaceFixture(window);QVERIFY(fixture);QTest::qWait(300);
 auto* editor=window.findChild<ParameterEditor*>();QVERIFY(editor);
 for(auto name:{"previewSidePanel","parameterSidePanel","catalogSidePanel"}){auto*panel=window.findChild<CollapsiblePanel*>(name);if(panel){auto before=panel->isCollapsed();panel->setCollapsed(!before);QTest::qWait(20);panel->setCollapsed(before);}}
 for(auto* box:window.findChildren<QComboBox*>())if(box->isVisible()){const auto p=box->mapTo(&window,QPoint());QVERIFY(p.x()>=0);QVERIFY(p.x()+box->width()<=window.width());}
 auto* preview=window.findChild<WorkflowPreviewPanel*>();QVERIFY(preview);
 QTRY_COMPARE_WITH_TIMEOUT(preview->controller()->state(),WorkflowPreviewController::State::Ready,15000);
 QTest::qWait(100);
 capture(window,QString("workflow-%1-%2-%3").arg(language,theme).arg(size.width()));
 for(auto* area:window.findChildren<QScrollArea*>())if(area->isVisible()&&area->widget()&&area->horizontalScrollBarPolicy()==Qt::ScrollBarAlwaysOff)
   qInfo()<<"HORIZONTAL_FIT"<<area->parentWidget()->metaObject()->className()<<area->viewport()->width()<<area->widget()->width();
 auto* output=preview->findChild<QComboBox*>("previewOutput");QVERIFY(output);
 QVERIFY2(output->width()>=96,qPrintable(QString("Output selector width=%1").arg(output->width())));
 for(auto* area:window.findChildren<QScrollArea*>())if(area->isVisible()&&area->widget()&&area->horizontalScrollBarPolicy()==Qt::ScrollBarAlwaysOff)
   QVERIFY2(area->widget()->width()<=area->viewport()->width(),area->parentWidget()->metaObject()->className());
}
void Acceptance::mixedPlotOperations_data(){QTest::addColumn<quint32>("seed");for(auto seed:{19u,2026u,73u})QTest::newRow(qPrintable(QString::number(seed)))<<seed;}
void Acceptance::mixedPlotOperations(){
 QFETCH(quint32,seed);setLanguage("zh");QVERIFY(ThemeManager::instance().apply(*qApp,"light").isEmpty());
 PlotWorkspace w;w.resize(1100,750);w.show();QRandomGenerator random(seed);QSignalSpy completed(&w,&PlotWorkspace::outputReady);QVector<qint64> memory;
 int operations=0;QElapsedTimer elapsed;elapsed.start();
 for(int round=0;round<60;++round){
  if(w.pageCount()==0)w.open(refs[random.bounded(6)],PlotWorkspace::Target::Page);
  QVERIFY(waitPlots(w));auto* tabs=w.findChild<QTabWidget*>("plotPages");auto*host=w.findChild<PlotLayoutWidget*>();QVERIFY(host);
  const auto ids=host->snapshot().order();const int action=random.bounded(10);
  switch(action){
  case 0:if(w.subplotCount()<4)w.open(refs[random.bounded(6)],PlotWorkspace::Target::Subplot);else if(w.pageCount()<5)w.open(refs[random.bounded(6)],PlotWorkspace::Target::Page);break;
  case 1:if(ids.size()>1)QVERIFY(host->moveCell(ids[random.bounded(ids.size())],{},PlotPlacement::Above,true));break;
  case 2:if(ids.size()>1)QVERIFY(host->moveCell(ids.front(),ids.last(),PlotPlacement::Swap));break;
  case 3:w.open(refs[random.bounded(6)],PlotWorkspace::Target::Overlay);if(ids.size()>1)QVERIFY(host->moveCell(ids.last(),{},PlotPlacement::Left,true));break;
  case 4:w.open(refs[random.bounded(6)],PlotWorkspace::Target::Replace);if(w.pageCount()>1){auto index=tabs->currentIndex();tabs->setCurrentIndex((index+1)%tabs->count());tabs->setCurrentIndex(index);}break;
  case 5:if(w.subplotCount()>1)w.closeSubplot();else if(w.pageCount()>1)w.closePage(tabs->currentIndex());else w.open(refs[0],PlotWorkspace::Target::Subplot);break;
  case 6:if(w.pageCount()>1)tabs->setCurrentIndex(random.bounded(w.pageCount()));else w.open(refs[1],PlotWorkspace::Target::Page);break;
  case 7:{auto style=w.currentView()->style();style.title=QString("seed %1 round %2").arg(seed).arg(round);style.x.manualRange=true;style.x.lower=25;style.x.upper=125;QVERIFY(w.currentView()->applyStyle(style).isEmpty());break;}
  case 8:host->resetArrangement();w.resize(900+random.bounded(400),600+random.bounded(250));break;
  case 9:if(w.pageCount()>1)w.closePage((tabs->currentIndex()+1)%tabs->count());else w.open(refs[2],PlotWorkspace::Target::Subplot);break;
  }++operations;
  QVERIFY(waitPlots(w));QVERIFY(w.currentReference().valid());QVERIFY(w.currentView()->hasPlot());
  host=w.findChild<PlotLayoutWidget*>();QVERIFY(host);QVERIFY(host->snapshot().valid());QCOMPARE(host->snapshot().order().size(),w.subplotCount());
  int selected=0;for(auto*title:titles(&w))selected+=title->isChecked();QCOMPARE(selected,1);
  QCOMPARE(w.findChildren<PlotView*>().size(),w.subplotCount()+1);
  const auto figure=w.figureCells(true);QCOMPARE(figure.size(),w.subplotCount());
  double area=0;for(const auto& cell:figure){area+=cell.area.width()*cell.area.height();QVERIFY(!cell.references.isEmpty());QSet<QString> unique;for(auto ref:cell.references){QVERIFY(!unique.contains(ref.key()));unique.insert(ref.key());}}
  QVERIFY(std::abs(area-1)<1e-9);if(round%10==9)memory.push_back(rssKiB());
 }
 const auto settled=completed.count();QTest::qWait(400);QCOMPARE(completed.count(),settled);
 qInfo()<<"MIXED"<<seed<<"operations"<<operations<<"elapsed_ms"<<elapsed.elapsed()<<"rss_kib_samples"<<memory<<"completions"<<completed.count();
 w.reset();QVERIFY(waitPlots(w));QCOMPARE(w.pageCount(),0);QCOMPARE(w.findChildren<PlotView*>().size(),1);
}
void Acceptance::staleContextMenuDoesNotTouchNewPage(){
 PlotWorkspace w;w.resize(900,700);w.show();w.open(refs[0],PlotWorkspace::Target::Page);w.open(refs[1],PlotWorkspace::Target::Subplot);QVERIFY(waitPlots(w));
 auto* host=w.findChild<PlotLayoutWidget*>();QVERIFY(host);auto id=host->snapshot().order().front();QPointer<PlotLayoutWidget> old(host);
 QTimer::singleShot(30,&w,[&]{w.closePage(0);w.open(refs[2],PlotWorkspace::Target::Page);if(auto*menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()))menu->close();});
 host->titleMenu(id,w.mapToGlobal(QPoint(100,100)));QVERIFY(old.isNull());QVERIFY(waitPlots(w));QCOMPARE(w.pageCount(),1);QCOMPARE(w.subplotCount(),1);QCOMPARE(w.currentReference().key(),refs[2].key());
}

void Acceptance::parameterCatalogFits_data(){
 QTest::addColumn<QString>("language");QTest::addColumn<int>("width");
 for(auto lang:{"zh","en"})for(int width:{200,320})QTest::newRow(qPrintable(QString("%1-%2").arg(lang).arg(width)))<<QString(lang)<<width;
}
void Acceptance::parameterCatalogFits(){
 QFETCH(QString,language);QFETCH(int,width);setLanguage(language);const auto registry=makeScientificRegistry();
 ParameterEditor editor;editor.resize(width,700);editor.show();int inspected=0;
 for(const auto& descriptor:registry.descriptors()){
  if(descriptor.key=="file_source")continue; // Covered through the dedicated import editor in the screen matrix.
  NodeDefinition node;node.id=QUuid::createUuid();node.method=descriptor.key;node.parameters=registry.normalize(descriptor.key,{}).parameters;
  editor.showNode(&node,registry);QTest::qWait(15);
  QCOMPARE(editor.parameters(),node.parameters);
  for(auto*area:editor.findChildren<QScrollArea*>())if(area->widget())QVERIFY2(area->widget()->width()<=area->viewport()->width(),qPrintable(descriptor.key));
  ++inspected;
 }
 qInfo()<<"PARAMETER_CATALOG"<<language<<width<<"methods"<<inspected;
}
void Acceptance::denseLegendStaysInsideAxes(){
 setLanguage("en");ResultsPage page(makeScientificRegistry());page.resize(1280,800);page.setDatabase(path);page.show();page.openRecord(scientificId);QTRY_COMPARE(page.recordId(),scientificId);QVERIFY(waitPlots(*page.workspace()));
 page.selectStep(1,false);auto*table=page.findChild<QTableView*>("resultOutputs");QVERIFY(table);auto*model=qobject_cast<QStandardItemModel*>(table->model());QVERIFY(model);
 for(int i=0;i<model->rowCount();++i)if(model->item(i,0)->isCheckable())model->item(i,0)->setCheckState(Qt::Checked);
 page.plotChecked(PlotWorkspace::Target::Subplot);QVERIFY(waitPlots(*page.workspace()));
 auto*view=page.workspace()->currentView();auto*plot=view->plotWidget();QVERIFY(plot->legend->itemCount()>=10);QTest::qWait(200);
 const auto check=[&]{return plot->axisRect()->rect().contains(plot->legend->outerRect());};
 QVERIFY2(check(),qPrintable(QString("axes=%1,%2 %3x%4 legend=%5,%6 %7x%8").arg(plot->axisRect()->rect().x()).arg(plot->axisRect()->rect().y()).arg(plot->axisRect()->rect().width()).arg(plot->axisRect()->rect().height()).arg(plot->legend->outerRect().x()).arg(plot->legend->outerRect().y()).arg(plot->legend->outerRect().width()).arg(plot->legend->outerRect().height())));
 capture(page,"dense-legend-scroll");const auto before=view->minimumHeight();auto style=view->style();style.legend=false;QVERIFY(view->applyStyle(style).isEmpty());QTest::qWait(80);QVERIFY(view->minimumHeight()<before);
 style.legend=true;QVERIFY(view->applyStyle(style).isEmpty());QTest::qWait(200);QVERIFY(check());
 const auto savedFont=style.legendFontSize;page.resize(1024,700);QTest::qWait(200);QVERIFY(check());QCOMPARE(view->style().legendFontSize,savedFont);
}
void Acceptance::jointSearchUsesVisibleNames(){
 setLanguage("zh");WorkflowDocument doc(makeScientificRegistry());
 const auto root=doc.addRoot("input_series").nodeId;
 const auto child=doc.addChild(root,ChildSlot::Down,"demean").nodeId;
 const auto other=doc.addRoot("input_series").nodeId;
 const auto joint=doc.addChild(child,ChildSlot::Down,"cross_spectrum").nodeId;
 QVERIFY(doc.setSources(root,{{"80001.csv",samples(256),{}},{"80002.csv",samples(256),{}},{"80004.csv",samples(256),{}}}).isEmpty());
 QVERIFY(doc.setSources(other,{{"Other series",samples(256),{}}}).isEmpty());
 QWidget owner;owner.resize(1280,800);owner.show();
 QPointer<JointInputDialog> picker=new JointInputDialog(&doc,joint,{},&owner);picker->openWithinOwner();QTest::qWait(80);
 auto*nodes=picker->findChild<QTreeWidget*>("jointSourceTree");auto*data=picker->findChild<QTableView*>("jointMemberTable");
 auto*ns=picker->findChild<QLineEdit*>("jointNodeSearch");auto*ds=picker->findChild<QLineEdit*>("jointDataSearch");
 auto*confirm=picker->findChild<QPushButton*>("jointInputConfirm");QVERIFY(nodes&&data&&ns&&ds&&confirm);
 const auto visible=[&]{int count=0;for(QTreeWidgetItemIterator it(nodes);*it;++it)if(!(*it)->isHidden())++count;return count;};
 // A UUID is guaranteed to exist internally, but is not a displayed node name.
 ns->setText(root.toString(QUuid::WithoutBraces));QCOMPARE(visible(),0);QCOMPARE(data->model()->rowCount(),0);QVERIFY(!confirm->isEnabled());
 ns->clear();ns->setFocus();QTest::keyClicks(ns,"6");QCOMPARE(visible(),0);QCOMPARE(data->model()->rowCount(),0);
 ns->clear();QTest::keyClicks(ns,"#2");QCOMPARE(visible(),2); // Matching child plus its ancestor.
 for(QTreeWidgetItemIterator it(nodes);*it;++it)if((*it)->data(0,Qt::UserRole).toUuid()==child)nodes->setCurrentItem(*it);
 QCOMPARE(data->model()->rowCount(),3);data->setCurrentIndex(data->model()->index(0,0));QVERIFY(confirm->isEnabled());
 ds->setFocus();QTest::keyClicks(ds,"5");QCOMPARE(data->model()->rowCount(),0);QVERIFY(!confirm->isEnabled());
 ds->setText(doc.datasets(root).first().id.toString(QUuid::WithoutBraces));QCOMPARE(data->model()->rowCount(),0);
 ds->clear();QTest::keyClicks(ds,"80002");QCOMPARE(data->model()->rowCount(),1);QCOMPARE(data->model()->index(0,0).data().toString(),QString("80002.csv"));
 ds->setText(QString::fromUtf8("数据序列"));QCOMPARE(data->model()->rowCount(),3);
 ds->setText("80004");data->setCurrentIndex(data->model()->index(0,0));QVERIFY(confirm->isEnabled());
 ns->setText("no such node");QCOMPARE(data->model()->rowCount(),0);QVERIFY(!confirm->isEnabled());
 capture(*picker,"joint-search-no-match");picker->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!picker);
}
void Acceptance::shortSinePairExplainsSegmentsAndRecovers(){
 setLanguage("zh");WorkflowDocument doc(makeScientificRegistry());
 const auto a=doc.addRoot("sine_source",{{"count",256},{"amplitude",3},{"period",8},{"slope",0}}).nodeId;
 const auto b=doc.addRoot("sine_source",{{"count",256},{"amplitude",2},{"period",4},{"slope",0}}).nodeId;
 QVERIFY(!a.isNull());QVERIFY(!b.isNull());
 const auto joint=doc.addChild(a,ChildSlot::Down,"cross_spectrum").nodeId;
 const auto pair=QUuid::createUuid();QVERIFY(doc.setPairs(joint,{{pair,"Sines",{doc.datasets(a).first().id,"series",a},{doc.datasets(b).first().id,"series",b}}}).isEmpty());
 WorkflowPreviewController preview(&doc);preview.selectNode(joint);preview.selectPair(pair);preview.setActive(true);
 QTRY_COMPARE_WITH_TIMEOUT(preview.state(),WorkflowPreviewController::State::Failed,10000);
 QVERIFY(preview.result());const auto error=preview.result()->output;
 QCOMPARE(error.diagnostic().code,QString("CROSS_SEGMENTS"));QCOMPARE(error.diagnostic().context.value("required_samples").toInt(),384);
 for(auto lang:{"zh","en"}){setLanguage(lang);const auto presented=presentError(error);QVERIFY(presented.summary.contains("256"));QVERIFY(presented.suggestion.contains("128"));QVERIFY(presented.suggestion.contains("64"));QVERIFY(!presented.summary.contains('{'));}
 // Edit the same parameter widgets used by the app, then undo/redo while a
 // preview request is pending. The final result must belong to the final edit.
 ParameterEditor editor;editor.showNode(doc.node(joint),doc.model().registry());editor.resize(280,700);editor.show();
 auto*segment=editor.findChild<QSpinBox*>("parameter_psd_window");auto*overlap=editor.findChild<QSpinBox*>("parameter_psd_overlap");
 QVERIFY(segment&&overlap);
 segment->setFocus();QTest::keyClick(segment,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(segment,"128");QTest::keyClick(segment,Qt::Key_Tab);
 overlap->setFocus();QTest::keyClick(overlap,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(overlap,"64");QTest::keyClick(overlap,Qt::Key_Tab);
 QVERIFY(doc.setParameters(joint,editor.parameters()).ok());QVERIFY(doc.undo().ok());QVERIFY(doc.redo().ok());
 QTRY_COMPARE_WITH_TIMEOUT(preview.state(),WorkflowPreviewController::State::Ready,10000);QVERIFY(preview.result());
 const auto result=preview.result()->output;QCOMPARE(result.report["segments"].toInt(),3);QCOMPARE(result.report["samples"].toInt(),256);
 QCOMPARE(result.report["segment_points"].toInt(),128);QCOMPARE(result.report["overlap_points"].toInt(),64);
 const auto direct=CrossSpectrumTask(editor.parameters()).execute({preview.result()->original.outputs["series"],preview.result()->second.outputs["series"]},{});
 QCOMPARE(direct.state,NodeState::Succeeded);const auto*x=dataAs<TableData>(direct.outputs["cross_spectrum"]);const auto*y=dataAs<TableData>(result.outputs["cross_spectrum"]);QVERIFY(x&&y);QCOMPARE(x->rows,y->rows);QCOMPARE(x->defined,y->defined);
 // Other invalid combinations receive actionable diagnostics too.
 for(const auto &test:QList<QPair<QJsonObject,QString>>{{{{"psd_window",512}},"CROSS_SEGMENT_LENGTH"},{{{"psd_window",128},{"psd_overlap",128}},"CROSS_OVERLAP"},{{{"psd_window",128},{"psd_overlap",64},{"psd_nfft",64}},"CROSS_NFFT"}}){
 const auto rejected=CrossSpectrumTask(test.first).execute({preview.result()->original.outputs["series"],preview.result()->second.outputs["series"]},{});QCOMPARE(rejected.diagnostic().code,test.second);QVERIFY(!presentError(rejected).suggestion.isEmpty());}
}
QTEST_MAIN(Acceptance)
#include "interaction_acceptance.moc"
