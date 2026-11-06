// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QTabBar>
#include "../tests/support/workspace_fixture.h"
#include "ui/panels/source_panel.h"
#include <QtTest>
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QMenu>
#include <QMessageBox>
#include <QAbstractButton>
#include <QTimer>
#include <QLabel>
#include <QFrame>
#include <QFormLayout>
#include <QListWidget>
#include <QDir>
#include <QHBoxLayout>
#include <QSpinBox>
#include "ui/results/grid_contours.h"
#include "ui/results/contour_plottable.h"
#include "ui/results/record_item_delegate.h"
#include "ui/settings/theme_manager.h"
#include "ui/exports/figure_export.h"
#include "processing/scientific_registry.h"
#include "core/workflow_executor.h"
#include "io/database/result_repository.h"
#include "ui/panels/canvas_details_editor.h"
#include "ui/panels/color_button.h"
#include <QLocale>
#include "io/exports/csv_export.h"
#include "ui/results/plot_view.h"
#include "ui/results/results_page.h"
#include "ui/panels/plot_style_editor.h"
#include "ui/services/export_controller.h"
#include "ui/common/presentation.h"
#include "ui/app/main_window.h"
#include "third_party/qcustomplot/qcustomplot.h"
using namespace aster;
using namespace aster::ui;
namespace
{
NodeResult spectrum()
{
    return NodeResult::success(
        {{"spectrum", makeData(SpectrumData{
                          {0, .1, .2, .3}, {0, 4, 1, 2}, {"Frequency", "1/ka"}, {"Power", "PSD"}})},
         {"swa_background",
          makeData(SpectrumData{
              {0, .1, .2, .3}, {1, 1, 1, 1}, {"Frequency", "1/ka"}, {"Power", "PSD"}})},
         {"peaks", makeData(TableData{{"frequency", "power"}, {{.1, 4}}})}});
}
NodeResult grid()
{
    return NodeResult::success({{"power", makeData(GridData{{0, 1, 2, 3},
                                                            {2, 4, 8},
                                                            {1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 5, 6},
                                                            3,
                                                            4,
                                                            {"Time", "ka"},
                                                            {"Period", "ka"},
                                                            {"Power", "u"}})}});
}
DataRef largeSeries(qsizetype n = 500000)
{
    SeriesData data;
    data.x.resize(n);
    data.y.resize(n);
    for (qsizetype i = 0; i < n; ++i)
    {
        data.x[i] = i;
        data.y[i] = i + .125;
    }
    data.xAxis = {"Time", "ka"};
    data.yAxis = {"Signal", "u"};
    return makeData(std::move(data));
}
QCustomPlot *plot(PlotView &view)
{
    return view.findChild<QCustomPlot *>("resultPlot");
}
QCPGraph *curve(PlotView &view, const QString &key)
{
    for (int i = 0; i < plot(view)->graphCount(); ++i)
    {
        auto *g = plot(view)->graph(i);
        if (g->property("outputKey").toString() == key)
            return g;
    }
    return nullptr;
}
QByteArray bytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
void original(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write("previous complete file\n");
}
} // namespace
class StyleExportTests : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase();
    void probeCurveClickAndLifetime();
    void probeGridCoordinatesAndMask();
    void firstPresentationWithoutInteraction();
    void themeSurfaceRoles();
    void contourGeometryAndBudgets();
    void contourRenderingAndState();
    void heatmapMappingPreservesRawData();
    void heatmapEditorLiveAndGrouped();
    void cwtDatabaseFamilyAndExport();
    void reducedSignificanceUsesCoordinateSamples();
    void recordDelegateKeepsIdentity();
    void liveStyleAndPendingViewSwitch();
    void canvasLabelsHaveCompactVisibleGeometry();
    void stylePreservesZoomAndData();
    void invalidStyleIsAtomic();
    void logarithmicAndReversedAxes();
    void gridLimitsUseOriginalCoordinates();
    void waveletCellsCoiAndExportCoordinates();
    void styleCacheIsolatesAndReleasesData();
    void oldCurveMenuCannotChangeNewOutput();
    void parameterEditorAppliesBothAxesAndCurves();
    void narrowInspectorLayout();
    void resultsPageStyleIntegration();
    void csvTypesPrecisionAndLocale();
    void csvCancellationKeepsExistingFile();
    void csvFailureLeavesExistingFile();
    void backgroundExportUsesCapturedData();
    void backgroundCancellationAndRestart();
    void exportCompletionBeforeRestart();
    void closeDuringExportWaits();
};
void StyleExportTests::probeCurveClickAndLifetime()
{
    PlotView view; view.resize(760,520); view.show(); view.showOutput(spectrum(),"spectrum");
    auto *p=plot(view); QTRY_VERIFY(p->axisRect()->width()>200); QTest::qWait(30);
    auto *bubble=p->findChild<QFrame *>("plotProbeBubble"); QVERIFY(bubble);
    auto *details=bubble->findChild<QLabel *>("plotProbeDetails"); QVERIFY(details);
    auto *g=curve(view,"peaks"); QVERIFY(g);
    const auto point=g->coordsToPixels(.1,4).toPoint();
    const auto cleanExport=p->toPixmap().toImage();
    QTest::mouseClick(p,Qt::LeftButton,Qt::NoModifier,point);
    QTRY_VERIFY(bubble->isVisible()); QVERIFY(details->text().contains("0.1"));
    QVERIFY(details->text().contains("10")); // period
    QVERIFY(details->text().contains("(ka): 10"));
    QVERIFY(p->rect().contains(bubble->geometry()));
    QCOMPARE(p->toPixmap().toImage(),cleanExport); // Inspection UI is absent from figure exports.
    const auto captures=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if(!captures.isEmpty()) { QDir().mkpath(captures); QVERIFY(view.grab().save(captures+"/probe_curve.png")); }
    QTest::keyClick(p,Qt::Key_Escape); QVERIFY(!bubble->isVisible());
    QTest::mouseClick(p,Qt::LeftButton,Qt::NoModifier,point); QVERIFY(bubble->isVisible());
    QTest::mouseClick(bubble->findChild<QToolButton *>("closePlotProbe"),Qt::LeftButton); QVERIFY(!bubble->isVisible());
    auto style=view.style(); style.swapXY=true; style.x.reversed=true;
    QVERIFY(view.applyStyle(style).isEmpty()); QTest::qWait(30);
    g=curve(view,"peaks"); QVERIFY(g);
    QTest::mouseClick(p,Qt::LeftButton,Qt::NoModifier,g->coordsToPixels(.1,4).toPoint());
    QVERIFY(bubble->isVisible()); QVERIFY(details->text().contains("Frequency (1/ka): 0.1"));
    QVERIFY(details->text().contains("Power (PSD): 4"));
    style.swapXY=false; style.x.reversed=false; QVERIFY(view.applyStyle(style).isEmpty()); QTest::qWait(30);
    g=curve(view,"peaks"); QVERIFY(g);
    QTest::mousePress(p,Qt::LeftButton,Qt::NoModifier,point);
    QTest::mouseMove(p,point+QPoint(40,30));
    QTest::mouseRelease(p,Qt::LeftButton,Qt::NoModifier,point+QPoint(40,30));
    QVERIFY(!bubble->isVisible());
    view.fit(); QTest::qWait(30);
    QTest::mouseClick(p,Qt::LeftButton,Qt::NoModifier,g->coordsToPixels(.1,4).toPoint());
    QVERIFY(bubble->isVisible());
    view.clear(); QVERIFY(!bubble->isVisible());
    QTest::mouseClick(p,Qt::LeftButton,Qt::NoModifier,point); QVERIFY(!bubble->isVisible());
    view.showOutput(spectrum(),"spectrum"); view.resize(190,110); QTest::qWait(30);
    QTest::mouseClick(p,Qt::LeftButton,Qt::NoModifier,curve(view,"peaks")->coordsToPixels(.1,4).toPoint());
    QVERIFY(!bubble->isVisible() || p->rect().contains(bubble->geometry()));
}
void StyleExportTests::probeGridCoordinatesAndMask()
{
    PlotView view; view.resize(760,560); view.show(); view.showOutput(grid(),"power");
    auto *p=plot(view); QTest::qWait(35);
    auto *bubble=p->findChild<QFrame *>("plotProbeBubble"); QVERIFY(bubble);
    for(bool swapped:{false,true}) {
        auto style=view.style(); style.swapXY=swapped; style.x.reversed=true; style.y.reversed=true;
        QVERIFY(view.applyStyle(style).isEmpty()); QTest::qWait(35);
        QCPColorMap *map=nullptr; for(int i=0;i<p->plottableCount();++i) if((map=qobject_cast<QCPColorMap *>(p->plottable(i)))) break;
        QVERIFY(map);
        QTest::mouseClick(p,Qt::LeftButton,Qt::NoModifier,map->coordsToPixels(1,view.style().y.logarithmic?2:4).toPoint());
        QVERIFY(bubble->isVisible());
        const auto detail=bubble->findChild<QLabel *>("plotProbeDetails")->text();
        QVERIFY(detail.contains("Time (ka): 1")); QVERIFY(detail.contains("Period (ka): 4")); QVERIFY(detail.contains("Power (u): 3"));
    }
    const auto captures=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if(!captures.isEmpty()) QVERIFY(view.grab().save(captures+"/probe_heatmap.png"));
    auto result=grid(); auto data=*dataAs<GridData>(result.outputs["power"]); data.values[5]=0;
    result.outputs["power"]=makeData(data); view.showOutput(result,"power");
    auto style=view.style(); style.colorLog=true; QVERIFY(view.applyStyle(style).isEmpty()); QTest::qWait(35);
    auto *map=qobject_cast<QCPColorMap *>(p->plottable(0)); QVERIFY(map);
    QTest::mouseClick(p,Qt::LeftButton,Qt::NoModifier,map->coordsToPixels(1,view.style().y.logarithmic?2:4).toPoint());
    QVERIFY(!bubble->isVisible());
}
void StyleExportTests::firstPresentationWithoutInteraction()
{
    PlotView view; view.resize(720,460);
    QSignalSpy paints(plot(view),&QCustomPlot::afterReplot);
    view.showOutput(spectrum(),"spectrum"); view.show();
    QTRY_VERIFY(paints.size()>0); QTRY_VERIFY(plot(view)->axisRect()->width()>300);
    QCOMPARE(view.plottedCount(),3);
    const int before=paints.size();
    view.hide(); view.showOutput(spectrum(),"spectrum"); view.resize(850,500); view.show();
    QTRY_VERIFY(paints.size()>before); QCOMPARE(view.plottedCount(),3);
    // Coordinate inspection alone must not trigger background database reload/refinement.
    view.setOutputReference({"unused.sqlite",1,QUuid::createUuid(),QUuid::createUuid(),"spectrum","Spectrum"});
    QTest::qWait(250); QSignalSpy requests(&view,&PlotView::viewportChanged);
    QTest::mouseClick(plot(view),Qt::LeftButton,Qt::NoModifier,curve(view,"peaks")->coordsToPixels(.1,4).toPoint());
    QTest::qWait(250); QCOMPARE(requests.size(),0);
}
void StyleExportTests::themeSurfaceRoles()
{
    const auto captures=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    for(const auto& name:QStringList{"light","dark"}) {
        QVERIFY(ThemeManager::instance().apply(*qApp,name).isEmpty());
        // Nested content shares one surface; grouping uses dividers and spacing.
        for (const char *role : {"header", "plot", "navigation", "inspector", "activity", "field"})
            QCOMPARE(themeColor(role), themeColor("surface"));
        QVERIFY(themeColor("window")!=themeColor("surface"));
        QVERIFY(themeColor("divider")!=themeColor("surface"));
        QVERIFY(themeColor("controlBorder")!=themeColor("divider"));
        for (const auto &role:QStringList{"navigation","inspector","plot","activity"}) {
            QWidget panel; panel.setObjectName("Panel"); panel.setProperty("panelRole",role);
            panel.ensurePolished(); QCOMPARE(panel.palette().color(QPalette::Window),themeColor(role.toLatin1().constData()));
        }
        QTemporaryDir directory; AppSettings settings; settings.databasePath=directory.filePath("ui.sqlite");
        settings.theme=name; MainWindow window(settings,false); window.resize(1600,950);
        window.show(); QTest::qWait(50);
        auto *sourceIdentity=window.findChild<QFrame *>("SourcePreviewIdentity");
        auto *preview=window.findChild<QWidget *>("workflowPreview");
        QVERIFY(sourceIdentity && preview);
        auto *resultIdentity=preview->findChild<QFrame *>("PreviewIdentity");
        QVERIFY(resultIdentity);
        QVERIFY(preview->isVisible());
        QVERIFY(sourceIdentity->isHidden());
        QVERIFY(resultIdentity->isHidden());
        if(!captures.isEmpty())
            QVERIFY(window.grab().save(captures+"/empty_"+name+".png"));
        auto files=aster::testsupport::installWorkspaceFixture(window); QVERIFY(files);
        QTRY_VERIFY(sourceIdentity->isVisible());
        auto *sourceTitle=sourceIdentity->findChild<QLabel *>("PreviewMethod");
        QVERIFY(sourceTitle && !sourceTitle->text().isEmpty());
        if(!captures.isEmpty()) {
            QTest::qWait(500);
            QVERIFY(window.grab().save(captures+"/workspace_"+name+".png"));
            QSignalSpy loaded(window.results(),&ResultsPage::recordLoaded);
            QTRY_VERIFY(window.findChild<SourcePanel *>()->readyToRun()); window.startRun();
            QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy() && !loaded.isEmpty(),10000);
            window.showResults(); QTRY_VERIFY(window.results()->plotView()->hasPlot());
            QTest::qWait(100);
            QVERIFY(window.grab().save(captures+"/results_"+name+".png"));
        }
    }
    QVERIFY(ThemeManager::instance().apply(*qApp,"light").isEmpty());
}
void StyleExportTests::initTestCase()
{
    setLanguage("zh");
    applyTheme(*qApp);
}
void StyleExportTests::stylePreservesZoomAndData()
{
    PlotView view;
    auto result = spectrum();
    view.showOutput(result, "spectrum", "one");
    plot(view)->xAxis->setRange(.05, .18);
    plot(view)->yAxis->setRange(.5, 4.5);
    auto *g = curve(view, "spectrum");
    QVERIFY(g);
    auto container = g->data();
    const auto raw = view.currentData();
    auto s = view.style();
    s.title = "Edited spectrum";
    s.x.label = "Frequency / ka";
    s.legendPosition = LegendPosition::BottomLeft;
    s.y.grid = false;
    s.curves["spectrum"].color = QColor("#aa3355");
    s.curves["spectrum"].width = 3;
    s.curves["spectrum"].pen = Qt::DashDotLine;
    s.curves["spectrum"].marker = MarkerStyle::Square;
    s.curves["swa_background"].visible = false;
    QVERIFY(view.applyStyle(s).isEmpty());
    QCOMPARE(curve(view, "spectrum"), g);
    QCOMPARE(g->data(), container);
    QCOMPARE(view.currentData(), raw);
    QCOMPARE(plot(view)->xAxis->range(), QCPRange(.05, .18));
    QCOMPARE(plot(view)->yAxis->range(), QCPRange(.5, 4.5));
    QCOMPARE(g->pen().color(), s.curves["spectrum"].color);
    QCOMPARE(g->pen().style(), Qt::DashDotLine);
    QVERIFY(!curve(view, "swa_background")->visible());
    QVERIFY(!plot(view)->yAxis->grid()->visible());
    QCOMPARE(plot(view)->legend->itemCount(), 2); // spectrum and peak markers
    QCOMPARE(plot(view)->axisRect()->insetLayout()->insetAlignment(0),
             Qt::Alignment(Qt::AlignBottom | Qt::AlignLeft));
    QTemporaryDir dir;
    QVERIFY(view.exportPng(dir.filePath("styled.png")));
    QVERIFY(QFileInfo(dir.filePath("styled.png")).size() > 1000);
}
void StyleExportTests::invalidStyleIsAtomic()
{
    PlotView view;
    view.showOutput(spectrum(), "spectrum");
    auto good = view.style(), bad = good;
    bad.title = "must not apply";
    bad.x.manualRange = true;
    bad.x.lower = 2;
    bad.x.upper = 1;
    QVERIFY(!view.applyStyle(bad).isEmpty());
    QCOMPARE(view.style().title, good.title);
    bad.x.lower = 0;
    bad.x.upper = 1;
    bad.x.logarithmic = true;
    QVERIFY(!view.applyStyle(bad).isEmpty());
    QVERIFY(!view.style().x.logarithmic);
    bad = good;
    bad.y.manualRange = true;
    bad.y.lower = qQNaN();
    bad.y.upper = 1;
    QVERIFY(!view.applyStyle(bad).isEmpty());
    bad = good;
    bad.curves["spectrum"].width = -1;
    QVERIFY(!view.applyStyle(bad).isEmpty());
    QCOMPARE(curve(view, "spectrum")->pen().widthF(), good.curves["spectrum"].width);
}
void StyleExportTests::logarithmicAndReversedAxes()
{
    PlotView view;
    auto data = spectrum();
    view.showOutput(data, "spectrum");
    auto s = view.style();
    s.x.logarithmic = s.y.logarithmic = true;
    s.x.reversed = true;
    s.x.manualRange = true;
    s.x.lower = .01;
    s.x.upper = 1;
    s.y.manualRange = true;
    s.y.lower = .1;
    s.y.upper = 10;
    QVERIFY(view.applyStyle(s).isEmpty());
    QCOMPARE(plot(view)->xAxis->scaleType(), QCPAxis::stLogarithmic);
    QVERIFY(plot(view)->xAxis->rangeReversed());
    QVERIFY(std::isnan(curve(view, "spectrum")->data()->constBegin()->value));
    QCOMPARE(dataAs<SpectrumData>(view.currentData())->power[0], 0.);
    view.resetStyle();
    QCOMPARE(plot(view)->xAxis->scaleType(), QCPAxis::stLinear);
    QCOMPARE(curve(view, "spectrum")->data()->constBegin()->value, 0.);
    QVERIFY(!plot(view)->xAxis->rangeReversed());
    auto negative = makeData(SeriesData{{0, 1}, {-1, -2}, {"x", ""}, {"y", ""}});
    view.showOutput(NodeResult::success({{"series", negative}}), "series");
    s = view.style();
    s.y.logarithmic = true;
    QVERIFY(!view.applyStyle(s).isEmpty());
}
void StyleExportTests::gridLimitsUseOriginalCoordinates()
{
    PlotView view;
    auto result = grid();
    view.showOutput(result, "power");
    auto s = view.style();
    QVERIFY(s.y.logarithmic);
    s.y.manualRange = true;
    s.y.lower = 2;
    s.y.upper = 4;
    s.y.reversed = true;
    QVERIFY(view.applyStyle(s).isEmpty());
    QCOMPARE(plot(view)->yAxis->range(), QCPRange(1, 2));
    auto captured = view.styleWithViewport();
    QCOMPARE(captured.y.lower, 2.);
    QCOMPARE(captured.y.upper, 4.);
    s.y.logarithmic = false;
    QVERIFY(view.applyStyle(s).isEmpty());
    QCOMPARE(plot(view)->yAxis->range(), QCPRange(2, 4));
    s.x.logarithmic = true;
    QVERIFY(!view.applyStyle(s).isEmpty());
    QCOMPARE(dataAs<GridData>(view.currentData())->y, QVector<double>({2, 4, 8}));
}
void StyleExportTests::waveletCellsCoiAndExportCoordinates()
{
    auto result = grid();
    result.outputs.insert("coi", makeData(SeriesData{{0,1,2,3},{0,2,4,0},{"Time","ka"},{"COI period","ka"}}));
    PlotView view; view.showOutput(result,"power");
    auto verify = [&] {
        QCPColorMap* map = nullptr;
        for (int i=0;i<plot(view)->plottableCount();++i)
            if (auto* candidate=qobject_cast<QCPColorMap*>(plot(view)->plottable(i))) map=candidate;
        if (!map) return false;
        if (map->data()->keySize()!=4 || map->data()->valueSize()!=3) return false;
        const auto* data=dataAs<GridData>(result.outputs["power"]);
        for (int r=0;r<3;++r) for(int c=0;c<4;++c)
            if (map->data()->cell(c,r)!=data->values[r*4+c]) return false;
        auto* line=curve(view,"coi");
        return line && line->data()->size()==4 && std::isnan(line->data()->at(0)->value) &&
            line->data()->at(1)->value==1 && line->data()->at(2)->value==2;
    };
    QVERIFY(verify());
    auto style=view.style(); style.swapXY=true; style.y.reversed=true;
    QVERIFY(view.applyStyle(style).isEmpty()); QVERIFY(verify());
    QTemporaryDir dir; const auto path=dir.filePath("wavelet.csv");
    QVERIFY(exportDataCsv(view.currentData(),path).ok());
    QVERIFY(bytes(path).contains("\n8,3,4,5,6\n")); // Original period 8, not display log2(period)=3.
    QCOMPARE(dataAs<GridData>(view.currentData())->y,QVector<double>({2,4,8}));
}
void StyleExportTests::styleCacheIsolatesAndReleasesData()
{
    PlotView view;
    auto first = spectrum();
    view.showOutput(first, "spectrum", "record-1/spectrum");
    auto s = view.style();
    s.title = "Only record 1";
    s.curves["spectrum"].color = Qt::red;
    QVERIFY(view.applyStyle(s).isEmpty());
    plot(view)->xAxis->setRange(.05, .15);
    view.showOutput(first, "spectrum", "record-2/spectrum");
    QVERIFY(view.style().title.isEmpty());
    view.showOutput(first, "spectrum", "record-1/spectrum");
    QCOMPARE(view.style().title, QString("Only record 1"));
    QCOMPARE(plot(view)->xAxis->range(), QCPRange(.05, .15));
    std::weak_ptr<const DataObject> weak = first.outputs["spectrum"];
    first = {};
    view.clear();
    QVERIFY(weak.expired());
    view.showOutput(spectrum(), "spectrum", "record-1/spectrum");
    QCOMPARE(view.style().curves["spectrum"].color, QColor(Qt::red));
    view.resetStyle();
    QVERIFY(view.style().title.isEmpty());
}
void StyleExportTests::oldCurveMenuCannotChangeNewOutput()
{
    PlotView view;
    view.showOutput(spectrum(), "spectrum", "first");
    auto *menu = view.curveMenu(&view);
    QVERIFY(!menu->actions().isEmpty());
    view.clear();
    view.showOutput(spectrum(), "spectrum", "second");
    auto before = view.style().curves;
    for (auto *action : menu->actions())
        action->setChecked(!action->isChecked());
    for (auto it = before.begin(); it != before.end(); ++it)
        QCOMPARE(view.style().curves[it.key()].visible, it->visible);
    delete menu;
}
void StyleExportTests::parameterEditorAppliesBothAxesAndCurves()
{
    PlotView view;
    view.showOutput(spectrum(), "spectrum");
    PlotStyleEditor editor(&view);
    editor.findChild<QLineEdit *>("plotTitle")->setText("Edited from controls");
    auto *label = editor.findChild<QLineEdit *>("axisLabel");
    label->setText("Frequency");
    auto *axis = editor.findChild<QTabBar *>("styleAxis");
    axis->setCurrentIndex(1);
    label->setText("Power density");
    auto *combo = editor.findChild<QComboBox *>("styleCurve");
    combo->setCurrentIndex(combo->findData("spectrum"));
    editor.findChild<QLineEdit *>("curveName")->setText("MTM");
    editor.findChild<QComboBox *>("curvePen")->setCurrentIndex(1);
    QVERIFY(editor.apply());
    QCOMPARE(view.style().x.label, QString("Frequency"));
    QCOMPARE(view.style().y.label, QString("Power density"));
    QCOMPARE(view.style().title, QString("Edited from controls"));
    QCOMPARE(curve(view, "spectrum")->name(), QString("MTM"));
    QCOMPARE(curve(view, "spectrum")->pen().style(), Qt::DashLine);
    auto precise = view.style();
    precise.x.manualRange = true;
    precise.x.lower = .012345678912345678;
    precise.x.upper = .21234567891234567;
    QVERIFY(view.applyStyle(precise).isEmpty());
    axis->setCurrentIndex(0);
    QVERIFY(editor.apply());
    QCOMPARE(plot(view)->xAxis->range(), QCPRange(precise.x.lower, precise.x.upper));
    axis->setCurrentIndex(1);
    editor.findChild<QCheckBox *>("axisManual")->setChecked(true);
    editor.findChild<QLineEdit *>("axisLower")->setText("100");
    editor.findChild<QLineEdit *>("axisUpper")->setText("1");
    QVERIFY(!editor.apply());
    QVERIFY(!view.style().y.manualRange);
}
void StyleExportTests::narrowInspectorLayout()
{
    PlotView view;
    view.showOutput(spectrum(), "spectrum");
    PlotStyleEditor editor(&view);
    editor.resize(275, 440);
    editor.show();
    auto *tabs = editor.findChild<QTabWidget *>("plotStyleSections");
    for (int i = 0; i < tabs->count(); ++i)
    {
        tabs->setCurrentIndex(i);
        QCoreApplication::processEvents();
        for (auto *area : editor.findChildren<QScrollArea *>())
            if (area->isVisible())
            {
                QCOMPARE(area->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
                QCOMPARE(area->horizontalScrollBar()->maximum(), 0);
            }
        for (auto *c : editor.findChildren<QComboBox *>())
            if (c->isVisible())
            {
                const auto p = c->mapTo(&editor, QPoint());
                QVERIFY(p.x() >= 0 && p.x() + c->width() <= editor.width());
            }
    }
}
void StyleExportTests::resultsPageStyleIntegration()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("results.sqlite");
    MainWindow window(settings, false);
    window.resize(1600, 900);
    window.show();
    window.showResults();
    auto *inspector = window.findChild<QTabWidget *>("resultInspectorTabs");
    auto *emptyEditor = window.findChild<PlotStyleEditor *>();
    QVERIFY(inspector && emptyEditor);
    QVERIFY(emptyEditor->isHidden());
    QVERIFY(window.findChild<QLabel *>("activeStyleTarget")->text().contains(text("ui.no_active_figure")));
    QTRY_VERIFY(inspector->tabBar()->width() >= inspector->width() - 4);
    QVERIFY(!inspector->tabBar()->usesScrollButtons());
    auto *target = window.findChild<QLabel *>("activeStyleTarget");
    QVERIFY(target->mapTo(inspector, QPoint()).y() < 150);
    window.setPage(0);
    auto fixture = aster::testsupport::installWorkspaceFixture(window);
    QVERIFY(fixture);
    QSignalSpy loaded(window.results(), &ResultsPage::recordLoaded);
    QTRY_VERIFY(window.findChild<SourcePanel *>()->readyToRun());
    window.startRun();
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy() && loaded.count() > 0, 10000);
    QTRY_VERIFY(window.results()->plotView()->hasPlot());
    QPointer<PlotView> view = window.results()->plotView();
    auto *editor = window.findChild<PlotStyleEditor *>();
    QVERIFY(editor);
    QVERIFY(editor->isVisible());
    QVERIFY(editor->height() > inspector->height() / 2);
    auto *sections = editor->findChild<QTabWidget *>("plotStyleSections");
    QVERIFY(inspector->tabBar()->height() > sections->tabBar()->height());
    QTRY_VERIFY(sections->tabBar()->width() >= sections->width() - 4);
    QVERIFY(!window.findChild<QLabel *>("activeStyleTarget")->text().contains(text("ui.no_active_figure")));

    const auto id = window.results()->recordId();
    QVERIFY(id > 0);
    editor->findChild<QLineEdit *>("plotTitle")->setText("MTM · Sample analysis");
    auto *axis = editor->findChild<QTabBar *>("styleAxis");
    axis->setCurrentIndex(0);
    editor->findChild<QLineEdit *>("axisLabel")->setText("Frequency");
    editor->findChild<QCheckBox *>("axisManual")->setChecked(true);
    editor->findChild<QLineEdit *>("axisLower")->setText("0");
    editor->findChild<QLineEdit *>("axisUpper")->setText("0.2");
    auto *selector = editor->findChild<QComboBox *>("styleCurve");
    selector->setCurrentIndex(selector->findData("spectrum"));
    editor->findChild<QLineEdit *>("curveName")->setText("MTM spectrum");
    editor->findChild<QDoubleSpinBox *>("curveWidth")->setValue(2.5);
    QVERIFY(editor->apply());
    const auto captureDir = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!captureDir.isEmpty())
    {
        QDir output(captureDir);
        QVERIFY(output.mkpath("."));
        auto *sections = editor->findChild<QTabWidget *>("plotStyleSections");
        sections->setCurrentIndex(2);
        QTest::qWait(40);
        QVERIFY(window.grab().save(output.filePath("curve_editor.png")));
        sections->setCurrentIndex(1);
        QTest::qWait(40);
        QVERIFY(window.grab().save(output.filePath("axis_editor.png")));
        window.resize(1200, 720);
        QTest::qWait(40);
        QVERIFY(window.grab().save(output.filePath("desktop_1200.png")));
    }
    const int before = loaded.count();
    window.results()->openRecord(id == 1 ? 2 : 1);
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > before, 10000);
    QTRY_VERIFY(window.results()->plotView()->hasPlot());
    QVERIFY(view.isNull());
    QVERIFY(window.results()->plotView()->style().title.isEmpty());
    window.results()->openRecord(id);
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > before + 1, 10000);
    QTRY_VERIFY(window.results()->plotView()->hasPlot());
    view = window.results()->plotView();
    QCOMPARE(view->style().title, QString("MTM · Sample analysis"));
    QCOMPARE(plot(*view)->xAxis->range(), QCPRange(0, .2));
    QCOMPARE(curve(*view, "spectrum")->pen().widthF(), 2.5);
    window.results()->selectStep(5);
    QTRY_VERIFY(!editor->isEnabled());
}
void StyleExportTests::csvTypesPrecisionAndLocale()
{
    QTemporaryDir dir;
    auto previous = QLocale();
    QLocale::setDefault(QLocale(QLocale::German));
    struct Restore
    {
        QLocale old;
        ~Restore()
        {
            QLocale::setDefault(old);
        }
    } restore{previous};
    auto source = makeData(
        SeriesData{{.125, .5}, {1.2345678901234567, -2.5}, {"x,\"quoted\"", "ka"}, {"信号", ""}});
    auto path = dir.filePath("中文.csv");
    QVERIFY(exportDataCsv(source, path).ok());
    auto csv = bytes(path);
    QVERIFY(csv.contains("\"x,\"\"quoted\"\" (ka)\""));
    QVERIFY(csv.contains("0.125,1.2345678901234567\n"));
    QVERIFY(csv.contains("0.5,-2.5\n"));
    QVERIFY(exportDataCsv(spectrum().outputs["spectrum"], path).ok());
    QVERIFY(bytes(path).contains("0,0\n"));
    QVERIFY(exportDataCsv(grid().outputs["power"], path).ok());
    QVERIFY(bytes(path).contains("\n8,3,4,5,6\n"));
    auto table = makeData(TableData{{"A", "B"}, {{1.5, 2.25}}});
    QVERIFY(exportDataCsv(table, path).ok());
    QVERIFY(bytes(path).contains("1.5,2.25\n"));
    QVERIFY(exportDataCsv(makeData(TableData{{"empty"}, {}}), path).ok());
    QCOMPARE(bytes(path), QByteArray("\"empty\"\n"));
}
void StyleExportTests::csvCancellationKeepsExistingFile()
{
    QTemporaryDir dir;
    auto path = dir.filePath("existing.csv");
    original(path);
    CancellationToken cancel;
    auto result = exportDataCsv(largeSeries(10000), path, cancel,
                                [&](int percent)
                                {
                                    if (percent > 0)
                                        cancel.cancel();
                                });
    QVERIFY(result.cancelled);
    QCOMPARE(bytes(path), QByteArray("previous complete file\n"));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot),
             QStringList({"existing.csv"}));
    auto absent = dir.filePath("new.csv");
    QVERIFY(exportDataCsv(largeSeries(1000), absent, cancel).cancelled);
    QVERIFY(!QFileInfo::exists(absent));
}
void StyleExportTests::csvFailureLeavesExistingFile()
{
    QTemporaryDir dir;
    auto path = dir.filePath("existing.csv");
    original(path);
    QVERIFY(!exportDataCsv(makeData(GridData{}), path).ok());
    QCOMPARE(bytes(path), QByteArray("previous complete file\n"));
    QVERIFY(!exportDataCsv(largeSeries(2), dir.filePath("absent/file.csv")).ok());
    auto failed = exportDataCsv(largeSeries(2), path, {},
                                [](int) { throw std::runtime_error("observer failed"); });
    QVERIFY(!failed.ok());
    QCOMPARE(bytes(path), QByteArray("previous complete file\n"));
}
void StyleExportTests::backgroundExportUsesCapturedData()
{
    QTemporaryDir dir;
    PlotView view;
    view.showOutput(NodeResult::success({{"series", largeSeries()}}), "series");
    ExportController controller;
    QSignalSpy done(&controller, &ExportController::finished);
    int ticks = 0;
    QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&] { ++ticks; });
    timer.start(1);
    bool guiThread = true;
    connect(&controller, &ExportController::progressChanged, this,
            [&](int) { guiThread &= QThread::currentThread() == qApp->thread(); });
    const auto path = dir.filePath("snapshot.csv");
    QVERIFY(controller.start(view.currentData(), path));
    QVERIFY(!controller.start(view.currentData(), dir.filePath("second.csv")));
    view.clear();
    view.showOutput(grid(), "power");
    QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1, 10000);
    QVERIFY(!controller.busy());
    QVERIFY(guiThread);
    QVERIFY(ticks > 0);
    QVERIFY(done.front()[1].toString().isEmpty());
    QVERIFY(!done.front()[2].toBool());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readLine(), QByteArray("\"Time (ka)\",\"Signal (u)\"\n"));
    file.seek(file.size() - 40);
    QVERIFY(file.readAll().endsWith("499999,499999.125\n"));
    QVERIFY(dataAs<GridData>(view.currentData()));
}
void StyleExportTests::backgroundCancellationAndRestart()
{
    QTemporaryDir dir;
    auto path = dir.filePath("kept.csv");
    original(path);
    ExportController c;
    QSignalSpy done(&c, &ExportController::finished);
    auto data = largeSeries();
    QVERIFY(c.start(data, path));
    c.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(done.size(), 1);
    QVERIFY(done[0][2].toBool());
    QCOMPARE(bytes(path), QByteArray("previous complete file\n"));
    QVERIFY(c.start(grid().outputs["power"], path));
    QTRY_VERIFY_WITH_TIMEOUT(done.size() == 2, 10000);
    QVERIFY(!done[1][2].toBool());
    QVERIFY(bytes(path).contains("\n8,3,4,5,6\n"));
}
void StyleExportTests::exportCompletionBeforeRestart()
{
    QTemporaryDir dir;
    ExportController controller;
    QStringList events;
    int idleCount = 0;
    connect(&controller, &ExportController::finished, this,
            [&](const QString &path, const QString &error, bool cancelled)
            {
                QVERIFY(error.isEmpty());
                QVERIFY(!cancelled);
                QVERIFY(controller.busy());
                events << QFileInfo(path).fileName();
                QVERIFY(!controller.start(largeSeries(4), dir.filePath("too-early.csv")));
            });
    connect(&controller, &ExportController::busyChanged, this, [&](bool busy)
            {
                if (!busy)
                {
                    events << "idle";
                    if (++idleCount == 1)
                        QVERIFY(controller.start(largeSeries(4), dir.filePath("second.csv")));
                }
            });
    QVERIFY(controller.start(largeSeries(4), dir.filePath("first.csv")));
    QTRY_COMPARE_WITH_TIMEOUT(idleCount, 2, 5000);
    QCOMPARE(events, QStringList({"first.csv", "idle", "second.csv", "idle"}));
    QVERIFY(!controller.busy());
}
void StyleExportTests::closeDuringExportWaits()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("close.sqlite");
    MainWindow window(settings, false);
    window.show();
    window.results()->plotView()->showOutput(NodeResult::success({{"series", largeSeries()}}),
                                             "series");
    auto path = dir.filePath("export.csv");
    original(path);
    QVERIFY(window.results()->startCsvExport(path));
    QTimer::singleShot(0,
                       []
                       {
                           if (auto *box =
                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                               QTest::mouseClick(box->button(QMessageBox::Yes), Qt::LeftButton);
                       });
    window.close();
    QTRY_VERIFY_WITH_TIMEOUT(!window.results()->exporter()->busy(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 10000);
    QCOMPARE(bytes(path), QByteArray("previous complete file\n"));
}
void StyleExportTests::liveStyleAndPendingViewSwitch()
{
    PlotView a, b;
    a.showOutput(spectrum(), "spectrum");
    b.showOutput(spectrum(), "spectrum");
    PlotStyleEditor editor(&a);
    editor.resize(320, 700);
    editor.show();
    QVERIFY(!editor.findChild<QPushButton *>("applyPlotStyle"));
    auto *legend = editor.findChild<QCheckBox *>("plotLegend");
    legend->setChecked(false);
    QTRY_VERIFY(!a.style().legend);
    auto *title = editor.findChild<QLineEdit *>("plotTitle");
    title->setFocus();
    QTest::keyClicks(title, "Live title");
    title->setCursorPosition(3);
    QTRY_COMPARE(a.style().title, QString("Live title"));
    QCOMPARE(title->cursorPosition(), 3);
    auto *graphBefore = curve(a, "spectrum");
    auto dataBefore = graphBefore->data();
    auto *curveSelector = editor.findChild<QComboBox *>("styleCurve");
    curveSelector->setCurrentIndex(curveSelector->findData("spectrum"));
    QCoreApplication::processEvents();
    QSignalSpy changes(&a, &PlotView::styleChanged);
    auto *width = editor.findChild<QDoubleSpinBox *>("curveWidth");
    for (int i = 0; i < 40; ++i) width->setValue(1 + i * .1);
    QTRY_COMPARE(a.style().curves["spectrum"].width, 4.9);
    QVERIFY(changes.size() <= 2); // Continuous input is coalesced, not forty full refreshes.
    QCOMPARE(curve(a, "spectrum"), graphBefore);
    QCOMPARE(curve(a, "spectrum")->data().data(), dataBefore.data());
    auto *background = editor.findChild<ColorButton *>("plotBackgroundColor");
    QVERIFY(background);
    background->setColor(QColor("#e6edf5"));
    QTRY_COMPARE(a.style().background, QColor("#e6edf5"));
    QVERIFY(!editor.findChild<QCheckBox *>("plotFollowTheme")->isChecked());
    editor.findChild<QCheckBox *>("axisManual")->setChecked(true);
    editor.findChild<QLineEdit *>("axisLower")->setText("100");
    editor.findChild<QLineEdit *>("axisUpper")->setText("1");
    QTRY_VERIFY(editor.findChild<QLabel *>("Error")->isVisible());
    QVERIFY(!a.style().x.manualRange); // The last valid plot is retained.
    editor.findChild<QLineEdit *>("axisLower")->setText("0");
    QTRY_VERIFY(a.style().x.manualRange);
    QVERIFY(!editor.findChild<QLabel *>("Error")->isVisible());
    title->setText("Belongs to A");
    editor.setView(&b);
    QCOMPARE(a.style().title, QString("Belongs to A"));
    QCOMPARE(b.style().title, QString());
    title->setText("Belongs to B");
    QTRY_COMPARE(b.style().title, QString("Belongs to B"));
    QCOMPARE(a.style().title, QString("Belongs to A"));
    editor.findChild<QPushButton *>("resetPlotStyle")->click();
    QCOMPARE(b.style().title, QString());
}
void StyleExportTests::canvasLabelsHaveCompactVisibleGeometry()
{
    PlotView view;
    view.showOutput(spectrum(), "spectrum");
    for (const auto &language : {QString("en"), QString("zh")})
    {
        setLanguage(language);
        PlotStyleEditor editor(&view);
        editor.resize(310, 820);
        editor.show();
        QTest::qWait(30);
        auto *details = editor.findChild<CanvasDetailsEditor *>();
        QVERIFY(details);
        auto *control = details->findChild<QDoubleSpinBox *>("plotAxisFontSize");
        auto *label = details->findChild<QLabel *>("plotAxisFontSizeLabel");
        QVERIFY(label);
        QVERIFY2(label->width() > 40, qPrintable(QString::number(label->width())));
        QVERIFY(label->height() < 3 * label->fontMetrics().height());
        QVERIFY(control->y() - label->geometry().bottom() < 24);
    }
    setLanguage("en");
}

void StyleExportTests::contourGeometryAndBudgets()
{
    GridData g{{0,2},{0,4},{0,2,0,2},2,2,{},{},{}};
    auto out=gridContours(g);
    QCOMPARE(out.state,ContourState::Ready); QCOMPARE(out.segments.size(),1);
    const auto line=out.segments[0];
    QCOMPARE(line.x1(),1.); QCOMPARE(line.x2(),1.);
    QCOMPARE(std::abs(line.y2()-line.y1()),4.);
    g={{0,1,2},{0,1,2},{0,0,0,0,2,0,0,0,0},3,3,{},{},{}};
    out=gridContours(g); QCOMPARE(out.segments.size(),4);
    QMap<QPair<double,double>,int> degree;
    for (auto edge:out.segments) { ++degree[{edge.x1(),edge.y1()}]; ++degree[{edge.x2(),edge.y2()}]; }
    QCOMPARE(degree.size(),4);
    for (int n:degree) QCOMPARE(n,2); // One closed diamond, no fabricated closure.
    out=gridContours(g,1,3); QCOMPARE(out.state,ContourState::TooDense); QVERIFY(out.segments.isEmpty());
    for (double v:{0.,1.,2.}) { g.values.fill(v); QCOMPARE(gridContours(g).state,ContourState::NoBoundary); }
    g.values[0]=qQNaN(); QCOMPARE(gridContours(g).state,ContourState::Invalid);
    g.values.fill(0); g.rows=4; QCOMPARE(gridContours(g).state,ContourState::Invalid);
    g={{0,1},{0,1},{2,0,0,2},2,2,{},{},{}};
    out=gridContours(g); QCOMPARE(out.segments.size(),2);
    QCOMPARE(out.segments[0].p1(),QPointF(.5,0)); QCOMPARE(out.segments[0].p2(),QPointF(1,.5));
    g.values={1.1,0,0,1.1}; out=gridContours(g);
    QCOMPARE(out.segments[0].p1().y(),0.); QCOMPARE(out.segments[0].p2().x(),0.); // Opposite saddle decision.
    g.values={std::nextafter(1.,0.),std::nextafter(1.,2.),0,1e308};
    out=gridContours(g); QCOMPARE(out.state,ContourState::Ready);
    for (const auto &line:out.segments) QVERIFY(std::isfinite(line.x1()) && std::isfinite(line.y2()));
}
void StyleExportTests::contourRenderingAndState()
{
    auto r=grid(); auto ratio=*dataAs<GridData>(r.outputs["power"]);
    ratio.values={0,0,0,0,0,2,2,0,0,0,0,0}; r.outputs["significance_ratio"]=makeData(ratio);
    r.outputs["coi"]=makeData(SeriesData{{0,1,2,3},{0,4,4,0},{},{}});
    PlotView view; view.resize(800,500); view.showOutput(r,"power");
    QVERIFY(view.hasSignificanceGrid()); QCOMPARE(view.significanceSegmentCount(),6);
    QVERIFY(view.contourError().isEmpty()); QCOMPARE(plot(view)->graphCount(),1);
    auto *contour=dynamic_cast<ContourPlottable *>(plot(view)->plottable(1)); QVERIFY(contour);
    bool lower=false, upper=false;
    for (auto edge:contour->segments()) for (auto point:{edge.p1(),edge.p2()})
    { lower |= point.y()==1.5; upper |= point.y()==2.5; } // Linear interpolation on log2(period).
    QVERIFY(lower && upper);
    auto s=view.style(); s.swapXY=true; s.y.reversed=true; s.colorLog=true;
    QVERIFY(view.applyStyle(s).isEmpty()); QCOMPARE(view.significanceSegmentCount(),6);
    s.significanceContour=false; QVERIFY(view.applyStyle(s).isEmpty());
    QCOMPARE(view.plottedCount(),2); // Heat map + COI remain.
    ratio.values.fill(.5); r.outputs["significance_ratio"]=makeData(ratio);
    view.showOutput(r,"power"); QCOMPARE(view.significanceSegmentCount(),0);
    QVERIFY(view.heatmapStatus().contains(text("ui.heatmap_contour_none")));
    ratio.x[0]=-1; r.outputs["significance_ratio"]=makeData(ratio);
    view.showOutput(r,"power"); QVERIFY(!view.contourError().isEmpty());
    view.clear(); QCOMPARE(view.significanceSegmentCount(),0); QVERIFY(!view.hasSignificanceGrid());
    view.showOutput(grid(),"power"); QVERIFY(view.heatmapStatus().contains(text("ui.heatmap_contour_missing")));
}
void StyleExportTests::heatmapMappingPreservesRawData()
{
    auto result=grid(); auto g=*dataAs<GridData>(result.outputs["power"]);
    g.values[0]=0; g.values[1]=-2; result.outputs["power"]=makeData(g);
    PlotView view; view.showOutput(result,"power");
    plot(view)->xAxis->setRange(.4,2.1);
    auto s=view.style(); s.colorLog=true; s.colorLevels=16; s.colorReverse=true; s.colorScaleVisible=false;
    QVERIFY(view.applyStyle(s).isEmpty()); QCOMPARE(plot(view)->xAxis->range().lower,.4);
    auto *map=qobject_cast<QCPColorMap *>(plot(view)->plottable(0)); QVERIFY(map);
    QCOMPARE(map->gradient().levelCount(),16); QVERIFY(!map->colorScale()->visible());
    QVERIFY(std::isnan(map->data()->cell(0,0))); QVERIFY(std::isnan(map->data()->cell(1,0)));
    QCOMPARE(map->data()->cell(2,0),3.); QCOMPARE(map->dataRange().lower,2.);
    QCOMPARE(dataAs<GridData>(view.currentData())->values,g.values);
    QTemporaryDir d; QVERIFY(view.exportCsv(d.filePath("raw.csv")).isEmpty());
    QVERIFY(bytes(d.filePath("raw.csv")).contains("0,-2,3,4"));
    s.colorManual=true; s.colorLower=0; s.colorUpper=10;
    QVERIFY(!view.applyStyle(s).isEmpty()); QVERIFY(!view.style().colorManual);
    s.colorManual=false; s.colorLog=false; QVERIFY(view.applyStyle(s).isEmpty());
    map=qobject_cast<QCPColorMap *>(plot(view)->plottable(0));
    QCOMPARE(map->data()->cell(0,0),0.); QCOMPARE(map->data()->cell(1,0),-2.);
    g.values.fill(0); result.outputs["power"]=makeData(g); view.showOutput(result,"power");
    s=view.style(); s.colorLog=true; QVERIFY(!view.applyStyle(s).isEmpty()); QVERIFY(!view.style().colorLog);
}
void StyleExportTests::heatmapEditorLiveAndGrouped()
{
    PlotView view; view.showOutput(grid(),"power");
    PlotStyleEditor editor(&view); editor.resize(310,800); editor.show();
    auto *tabs=editor.findChild<QTabWidget *>("plotStyleSections"); QVERIFY(tabs->isTabVisible(3));
    tabs->setCurrentIndex(3);
    editor.findChild<QSpinBox *>("heatmapLevels")->setValue(24);
    editor.findChild<QComboBox *>("heatmapMapping")->setCurrentIndex(1);
    QTRY_VERIFY(view.style().colorLog && view.style().colorLevels==24);
    editor.findChild<QCheckBox *>("heatmapReverse")->click(); QTRY_VERIFY(view.style().colorReverse);
    QVERIFY(!editor.findChild<QCheckBox *>("heatmapContour")->isEnabled());
    QVERIFY(editor.findChildren<QFrame *>("StyleSection").size()>=12);
    auto *field=editor.findChild<QLineEdit *>("heatmapLower"); QVERIFY(!field->isEnabled());
    QCOMPARE(field->text().toDouble(),1.);
    auto refined=grid(); auto positive=*dataAs<GridData>(refined.outputs["power"]);
    for (auto &value:positive.values) value*=2;
    refined.outputs["power"]=makeData(positive); view.refreshOutput(refined);
    QCOMPARE(field->text().toDouble(),2.); QVERIFY(view.style().colorLog);
    editor.findChild<QCheckBox *>("heatmapManual")->click(); QVERIFY(field->isEnabled());
    field->setText("0.5"); editor.findChild<QLineEdit *>("heatmapUpper")->setText("5");
    QTRY_VERIFY(view.style().colorManual); QCOMPARE(view.style().colorLower,.5);
    view.showOutput(spectrum(),"spectrum"); QVERIFY(!tabs->isTabVisible(3));
    QVERIFY(tabs->currentIndex()!=3); QVERIFY(!view.style().colorLog);
    tabs->setCurrentIndex(2);
    auto *fill=editor.findChild<QCheckBox *>("curveFill");
    auto *color=editor.findChild<ColorButton *>("curveFillColor"); QVERIFY(!color->isEnabled());
    fill->click(); QTRY_VERIFY(color->isEnabled());
}
void StyleExportTests::cwtDatabaseFamilyAndExport()
{
    SeriesData data;
    for (int i=0;i<256;++i) { data.x<<i; data.y<<std::exp(-std::pow((i-128.)/45.,2))*std::sin(i*6.28318530718/12.); }
    WorkflowModel model(makeScientificRegistry()); auto root=model.addRoot("input_series",{0,0}).nodeId;
    auto cwt=model.addChild(root,ChildSlot::Down,"cwt").nodeId;
    const auto run=WorkflowExecutor().run({model.freeze().snapshot,root,{"wavelet",makeData(data),{}}});
    QCOMPARE(run.nodes[cwt].state,NodeState::Succeeded); QVERIFY(run.nodes[cwt].outputs.contains("significance_ratio"));
    QTemporaryDir dir; const auto path=dir.filePath("cwt.sqlite"); ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty()); const auto receipt=repo.save(run); QVERIFY(receipt.saved());
    OutputReference ref{path,receipt.recordId,run.runId,cwt,"power","Grid"};
    const auto full=repo.loadPlot(ref); QCOMPARE(full.state,NodeState::Succeeded);
    QVERIFY(full.outputs.contains("coi")); QVERIFY(full.outputs.contains("significance_ratio"));
    PlotRequest req; const auto preview=repo.loadPlotPreview(ref,req);
    QCOMPARE(preview.state,NodeState::Succeeded); QVERIFY(preview.outputs.contains("significance_ratio"));
    req.includeFamily=false; QCOMPARE(repo.loadPlotPreview(ref,req).outputs.size(),1);
    PlotView view; view.showOutput(preview,"power"); QVERIFY(view.significanceSegmentCount()>0);
    view.resize(850,540); view.show(); QTest::qWait(30);
    FigureCell cell{{ref},view.style(),view.plotRequest()}; FigureOptions opt;
    opt.pixels={900,600}; opt.dpi=100; opt.path=dir.filePath("wavelet.png");
    QVERIFY2(renderFigure({cell},{full},opt).isEmpty(),"PNG export failed");
    QImage image(opt.path); QVERIFY(!image.isNull());
    opt.path=dir.filePath("wavelet.pdf"); opt.format=FigureOptions::Format::Pdf;
    QVERIFY(renderFigure({cell},{full},opt).isEmpty()); QVERIFY(bytes(opt.path).startsWith("%PDF"));
    const auto capture=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!capture.isEmpty())
    {
        QDir out(capture); QVERIFY(out.mkpath("."));
        QWidget panel; auto *layout=new QHBoxLayout(&panel);
        auto *plotView=new PlotView; plotView->showOutput(preview,"power"); layout->addWidget(plotView,1);
        auto *editor=new PlotStyleEditor(plotView); editor->setFixedWidth(330); layout->addWidget(editor);
        editor->findChild<QTabWidget *>("plotStyleSections")->setCurrentIndex(3);
        panel.resize(1280,800); panel.show(); QTest::qWait(50);
        QVERIFY(panel.grab().save(out.filePath("heatmap_light.png")));
        QVERIFY(ThemeManager::instance().apply(*qApp,"dark").isEmpty()); QTest::qWait(50);
        QVERIFY(panel.grab().save(out.filePath("heatmap_dark.png")));
        QVERIFY(ThemeManager::instance().apply(*qApp,"light").isEmpty());
    }
}
void StyleExportTests::reducedSignificanceUsesCoordinateSamples()
{
    SeriesData data; for (int i=0;i<128;++i) { data.x<<i; data.y<<std::sin(i*.5); }
    WorkflowModel model(makeScientificRegistry()); auto root=model.addRoot("input_series",{0,0}).nodeId;
    auto id=model.addChild(root,ChildSlot::Down,"cwt").nodeId;
    auto run=WorkflowExecutor().run({model.freeze().snapshot,root,{"wavelet",makeData(data),{}}});
    QTemporaryDir d; ResultRepository repo; auto path=d.filePath("cwt.sqlite"); QVERIFY(repo.open(path).isEmpty());
    auto receipt=repo.save(run); QVERIFY(receipt.saved()); OutputReference ref{path,receipt.recordId,run.runId,id,"power","Grid"};
    PlotRequest req; req.columns=16; req.rows=8;
    auto result=repo.loadPlotPreview(ref,req); QCOMPARE(result.state,NodeState::Succeeded);
    const auto *raw=dataAs<GridData>(run.nodes[id].outputs["significance_ratio"]);
    const auto *small=dataAs<GridData>(result.outputs["significance_ratio"]); QVERIFY(raw && small);
    QVERIFY(result.report["display_reduced"].toBool());
    for (qsizetype r=0;r<small->rows;++r) for (qsizetype c=0;c<small->columns;++c)
    { auto sr=raw->y.indexOf(small->y[r]),sc=raw->x.indexOf(small->x[c]); QVERIFY(sr>=0 && sc>=0);
      QCOMPARE(small->values[r*small->columns+c],raw->values[sr*raw->columns+sc]); }
}
void StyleExportTests::recordDelegateKeepsIdentity()
{
    QListWidget list; list.setObjectName("resultRecords"); list.resize(280,400);
    list.setItemDelegate(new RecordItemDelegate(&list)); list.setUniformItemSizes(true);
    auto *item=new QListWidgetItem("ID 42 · time\nLong_sample_name.csv",&list);
    item->setData(RecordRole::Id,42); item->setData(RecordRole::FileName,"Long_sample_name.csv");
    item->setData(RecordRole::StartedAt,QDateTime::fromString("2026-11-02T10:30:00Z",Qt::ISODate));
    item->setData(RecordRole::State,"Succeeded"); list.show(); QTest::qWait(20);
    QSignalSpy spy(&list,&QListWidget::itemClicked);
    QTest::mouseClick(list.viewport(),Qt::LeftButton,Qt::NoModifier,list.visualItemRect(item).center());
    QCOMPARE(spy.count(),1); QCOMPARE(list.currentItem()->data(Qt::UserRole).toLongLong(),42);
    QVERIFY(list.visualItemRect(item).height()>=72);
}
QTEST_MAIN(StyleExportTests)
#include "style_export_tests.moc"
