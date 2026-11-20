#include "ui/panels/record_filter_panel.h"
#include "ui/results/record_item_delegate.h"
// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include "ui/help/help_dialog.h"
#include "ui/help/help_catalog.h"
#include <QElapsedTimer>
#include <QProgressBar>
#include <QSqlQuery>
#include <QAbstractTextDocumentLayout>
#include "ui/settings/settings_dialog.h"
#include <QTextBrowser>
#include <QDesktopServices>
#include <QPointer>
#include "ui/common/task_drag_preview.h"
#include "ui/common/numeric_editor.h"
#include "ui/common/error_presenter.h"
#include "ui/common/choice_editor.h"
#include "ui/common/output_picker.h"
#include "ui/common/popup_placement.h"
#include "ui/panels/plot_style_editor.h"
#include <QTabBar>
#include <QToolTip>
#include <QHelpEvent>
#include <QPushButton>
#include "ui/results/record_directory_panel.h"
#include "ui/common/plot_axis_hints.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include "../tests/support/workspace_fixture.h"
#include <QtTest>
#include "ui/panels/joint_input_dialog.h"
#include <QTableView>
#include <QtTest/qtestspontaneevent.h>
#include <QApplication>
#include <QListWidget>
#include <QScopeGuard>
#include <QtMath>
#include <cmath>
#include "ui/settings/theme_manager.h"
#include <QSplitter>
#include "ui/panels/workflow_preview_panel.h"
#include "ui/panels/collapsible_panel.h"
#include "ui/services/workflow_preview_controller.h"
#include "ui/results/preview_data.h"
#include "io/database/data_codec.h"
#include "io/exports/export_path.h"
#include "ui/exports/figure_export_dialog.h"
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QContextMenuEvent>
#include <QTableWidget>
#include <QTableView>
#include <QTreeWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QFile>
#include <QThread>
#include <QTimer>
#include <QMessageBox>
#include <QAbstractButton>
#include <QKeySequence>
#include <QToolButton>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QGraphicsItem>
#include <QWheelEvent>
#include <QLabel>
#include "ui/panels/file_options_editor.h"
#include "ui/services/record_directory_service.h"
#include "ui/app/main_window.h"
#include "ui/common/presentation.h"
#include "ui/common/source_provenance.h"
#include "ui/workflow/workflow_document.h"
#include "ui/workflow/workflow_canvas.h"
#include "ui/workflow/workflow_layout.h"
#include "ui/panels/parameter_editor.h"
#include "ui/panels/task_catalog.h"
#include "ui/panels/source_panel.h"
#include "ui/results/results_page.h"
#include "processing/scientific_registry.h"
#include "model/workflow_codec.h"
#include "third_party/qcustomplot/qcustomplot.h"
using namespace aster;
using namespace aster::ui;
namespace
{
QString unwrappedFileToolTip(const QListWidgetItem *item)
{
    QTextDocument document;
    document.setHtml(item->toolTip());
    return document.toPlainText().remove('\n');
}

QColor viewColorAt(QWidget *view, QPoint point)
{
    const auto shot = view->grab();
    const qreal scale = shot.devicePixelRatio();
    const QPoint pixel(qFloor((point.x() + .5) * scale), qFloor((point.y() + .5) * scale));
    return shot.toImage().pixelColor(pixel);
}
DataRef series()
{
    return makeData(SeriesData{{0, 1, 2, 3}, {1, 4, 2, 5}, {"Time", "ka"}, {"Signal", "u"}});
}
RunResult storedRun(QString label)
{
    WorkflowModel m(makeScientificRegistry());
    auto id = m.addRoot("sine_source", {0, 0}).nodeId;
    m.addChild(id, ChildSlot::Down, "series_summary");
    return WorkflowExecutor().run({m.freeze().snapshot, id, {label, {}, {}}});
}
NodeResult spectrumResult()
{
    return NodeResult::success(
        {{"spectrum", makeData(SpectrumData{
                          {0, .1, .2, .3}, {0, 4, 1, 2}, {"Frequency", "1/ka"}, {"Power", "PSD"}})},
         {"swa_background",
          makeData(SpectrumData{
              {0, .1, .2, .3}, {1, 1, 1, 1}, {"Frequency", "1/ka"}, {"Power", "PSD"}})},
         {"peaks", makeData(TableData{{"frequency", "power"}, {{.1, 4}}})}});
}
NodeResult gridResult()
{
    return NodeResult::success(
        {{"power", makeData(GridData{{0, 1, 2, 3},
                                     {2, 4, 8},
                                     {1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 5, 6},
                                     3,
                                     4,
                                     {"Time", "ka"},
                                     {"Period", "ka"},
                                     {"Power", "u"}})},
         {"coi",
          makeData(SeriesData{{0, 1, 2, 3}, {0, 3, 3, 0}, {"Time", "ka"}, {"Period", "ka"}})}});
}
QAction *findAction(QMenu *menu, const QString &key)
{
    for (auto *a : menu->actions())
    {
        if (a->data().toString() == key)
            return a;
        if (a->menu())
            if (auto *found = findAction(a->menu(), key))
                return found;
    }
    return nullptr;
}
class SlowSource : public Task
{
  public:
    NodeResult execute(const TaskInput &, const ExecutionContext &c) override
    {
        for (int i = 0; i < 10000; ++i)
        {
            if (c.cancellation.isCancelled())
                return NodeResult::cancelled();
            QThread::msleep(1);
        }
        return NodeResult::success({{"series", series()}});
    }
};
} // namespace
class UiTests : public QObject
{
    Q_OBJECT
    QList<QUrl> capturedHelpUrls;
  private slots:
    void outputPopupPlacement();
    void outputPickerInteraction_data();
    void outputPickerInteraction();
    void outputPickerFilteringAndCancellation();
    void outputPickerWorkflowBinding();
    void maskedTableDisplay();
    void captureHelpUrl(const QUrl &url) { capturedHelpUrls << url; }
    void settingsLinksAndKeyboardNavigation();
    void plotDestinationSettings();
    void helpCatalogueCoverageAndContext();
    void helpLinksAndFallback();
    void helpLanguageAndChapterReuse();
    void helpReferenceVisibleLayout();
    void helpDirectoryLinks();
    void helpTreeAndParameterSelection();
    void helpGlobalSearchAndCancellation();
    void helpSearchHighlightsFollowTheArticle();
    void hoverCardsRenderLiteralFileDetails();
    void stepStatusRetainsColorWhenSelected();
    void helpFindAndReadingHistory();
    void helpNavigationResponsiveLayout();
    void allTaskGuidesAndIndexStayOffline();
    void chronologyGuidesStayOfflineAndReturn();
    void statisticalGuidesStayOfflineAndReturn();
    void dataProcessingGuidesStayOfflineAndReturn();
    void spectralGuidesStayOfflineAndShareConcepts();
    void samplingGuidesStayOfflineAndReturnToParameter();
    void licenseNoticesOffline();
    void parameterHelpTracksSelectedNode();
    void fileReadinessSurvivesDisabledPanel();
    void compactInputStatesAndFrequencyEditor();
    void inputListDistinguishesHoverAndSelection_data();
    void inputListDistinguishesHoverAndSelection();
    void resultListSeparatesOpenCheckAndHover_data();
    void resultListSeparatesOpenCheckAndHover();
    void legacyFrequencyZeroCommit_data();
    void legacyFrequencyZeroCommit();
    void evolutionSettingsAndPlotDefaults();
    void frequencyLimitTypingPreservesDecimals_data();
    void frequencyLimitTypingPreservesDecimals();
    void cocoWorkBudgetPresentation();
    void sourceNodePreviewBeforeAddingAnalysis();
    void visibleSelectionActionsPreserveFilterScope();
    void axisHintsPreserveSingleAxisNavigation();
    void previewRecoversAfterRunAndRereadsFile();
    void wheelRequiresDeliberateNumericFocus();
    void axisSegmentsStayVisibleAndKeepBothAxes();
    void catalogSingleClickAndTaskDoubleClick();
    void dragCardThemeScaleAndSourceDrop();
    void restoredCatalogueMatchesMenusAndDrag();
    void restoredFilePickerCommitsAndCancels();
    void restoredPreviewAndExport_data();
    void restoredPreviewAndExport();
    void restoredOrbitalSourcesPreview();
    void cocoPreview();
    void cocoAnalysisPreview_data();
    void cocoAnalysisPreview();
    void cycleTuningPreview_data();
    void cycleTuningPreview();
    void chronologyPreview_data();
    void chronologyPreview();
    void previewFileErrorsAndInputReplacement();
    void previewRunsOnlyAncestorsAndSelectedInput();
    void previewUsesTheSelectedParentPort();
    void generatedSourcePreviewFollowsBranch();
    void generatedSourcePreviewKeepsOutputChoice();
    void generatedSourcePreviewCanvasRoundTrip();
    void generatedSourcePreviewHandlesTableOutputs();
    void generatedSourcePreviewSurvivesDownstreamFailure();
    void sourceCardFollowsSelectedBranch();
    void previewCancelsStaleWorkAndKeepsUiResponsive();
    void previewIgnoresUnrelatedEditsAndEmptyClicks();
    void previewUsesDraftFileOptionsAndAutoParameters();
    void previewFailureDeletionAndGenerator();
    void previewMultipleOutputsAndDisplayBudget();
    void previewCloseAndDesktopCapture();
    void parameterLayoutDefaultsAndVisibility();
    void menuTouchTargetsAndPeakMarkers();
    void peakFilterLeavesFigureIndependent_data();
    void peakFilterLeavesFigureIndependent();
    void compactDirectoryAndPeakPeriods();
    void exactIdSearchLocatesAndOpensConfirmedMatch();
    void closeWaitsForDatabaseEdits();
    void fileOptionsRemainPerInput();
    void directoryServiceQueriesAndMutations();
    void cancelAndCloseDuringLowess_data();
    void cancelAndCloseDuringLowess();
    void liveDirectoryDuringLargeFileBatch_data();
    void liveDirectoryDuringLargeFileBatch();
    void directoryServiceReentrantEnqueue();
    void directoryServiceReentrantEnqueue_data();
    void directoryServiceDestroyedByObserver();
    void recordNameExportIncludesUnloadedRows();
    void recordNameExportButtonAndDialog();
    void nodeActionsAndFrequencyFilter();
    void initTestCase();
    void draftBindingsSurviveUndo();
    void sourceBindingsAreSeparate();
    void scopedParametersAndMove();
    void scopedParameterEditAcceptsBorrowedNodeId();
    void scopedEditingAndPointReview();
    void selectedImportRulesAndApplyAllReachStoredOutputs();
    void movedInputKeepsNavigationAndHistory();
    void outlierLegendUsesDisplayLabels();
    void scopedMetadataDoesNotCopySamples();
    void batchImportSkipsIncompatibleFiles();
    void batchImportCapturesSelectionAndLatestEdit();
    void revertedImportEditLeavesOtherFilesUntouched();
    void reimportingAPathStartsWithANewStatus();
    void filePreviewPreservesFiniteValues();
    void batchImportKeyboardMouseScope();
    void batchImportPreservesConcurrentOptions();
    void sourceActionsFitNarrowPanel();
    void evolutionPresetIsEditable();
    void automaticEvolutionScopes();
    void batchPreservesInheritedDecisions();
    void batchRejectsStaleScope();
    void automaticScopeReplayIsolation();
    void automaticBatchFailureIsolation();
    void removeInputFlushesPendingParameters();
    void duplicateSourceSelectionUsesIdentity();
    void pairManagerFlushesPendingParameters();
    void helpSearchDiscardsStaleResults();
    void helpColdSearchEnterOpensResult();
    void automaticEvolutionBatchButtons();
    void previewMessagesRemainReadable();
    void evolutionResolvedPreviewFollowsInput();
    void batchImportDoesNotModifyReimportedDataset();
    void hierarchicalTaskMenu();
    void astronomySourcesUseSharedUi();
    void parameterValuesAreIndependent();
    void parameterRoundTrip_data();
    void parameterRoundTrip();
    void scientificParameterTyping();
    void scientificParametersReachPreviewAndRun();
    void verticalLayoutPreservesWorkflow();
    void persistentAddControlsUseTheirOwnParent();
    void appendEightChildrenFromCanvasAndCatalogue();
    void exampleSourcePreviewAndLabels();
    void syntheticSourcePreviewAndParameters();
    void canvasClickAndAtomicDelete();
    void canvasRejectsIllegalDrop();
    void canvasFractionalMtmAndExtremeZoom();
    void backgroundEventsAndCancellation();
    void latestResultRequestWins();
    void resultLoaderDoesNotRecreateMissingDatabase();
    void typedPlotAndRepeatedClear();
    void contextMenuAfterClear();
    void exportPreservesRawValues();
    void exportNamesUseRecordMetadata();
    void exportDialogsFollowTheirOwnTargets();
    void gridCoordinatesAndInvalidInput();
    void fullWindowRunUsesEditedParameters();
    void smallDesktopLayout();
    void closeDuringRunWaitsForWorker();
};
void UiTests::helpCatalogueCoverageAndContext()
{
    const auto registry = makeScientificRegistry();
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        for (const auto &d : registry.descriptors())
        {
            QVERIFY2(!methodHelp(d.key).isEmpty(), qPrintable(d.key));
            QVERIFY2(!methodHelp(d.key).startsWith("methodhelp."), qPrintable(d.key));
            QVERIFY2(localHelpAvailable(methodHelpTopic(d.key)), qPrintable(d.key));
            for (const auto &p : d.parameters)
            {
                if (p.editorHidden) continue;
                const QString context = d.key + '.' + p.key;
                QVERIFY2(!parameterHelp(p, d.key).isEmpty(), qPrintable(context));
                QVERIFY2(!parameterHelp(p, d.key).startsWith("parameterhelp."), qPrintable(context));
                QVERIFY2(!parameterLabel(p, d.key).startsWith("parameter."), qPrintable(context));
                QVERIFY2(localHelpAvailable(parameterHelpTopic(d.key, p.key)), qPrintable(context));
            }
        }
        ParameterSpec unknown; unknown.key = "unregistered_help_parameter";
        QVERIFY(parameterHelp(unknown, "unregistered_method").isEmpty());
        unknown.help = "Fallback explanation";
        QCOMPARE(parameterHelp(unknown, "unregistered_method"), unknown.help);
        QVERIFY(methodHelp("unregistered_method").isEmpty());
    }
    setLanguage("zh");
    ParameterSpec window; window.key = "smooth_window";
    QVERIFY(parameterLabel(window, "mtm").contains(QString::fromUtf8("比例")));
    QVERIFY(parameterLabel(window, "moving_average").contains(QString::fromUtf8("点数")));
    QVERIFY(parameterHelp(window, "mtm") != parameterHelp(window, "moving_average"));
}

void UiTests::helpTreeAndParameterSelection()
{
    struct Restore{~Restore(){setLanguage("zh");}} restore;
    const auto registry=makeScientificRegistry();
    HelpDialog dialog;dialog.show();
    auto *tree=dialog.findChild<QTreeWidget*>("helpContents");QVERIFY(tree);
    for(const auto &lang:QStringList{"zh","en"}){
        setLanguage(lang);QVERIFY(dialog.navigate(userGuideTopic()));
        QCOMPARE(tree->topLevelItemCount(),5);
        QVERIFY(dialog.findChild<QLineEdit*>("helpGlobalSearch")->placeholderText()==text("help.search_all"));
        for(const auto &method:registry.descriptors()){
            QVERIFY(dialog.navigate(methodHelpTopic(method.key)));
            auto *item=tree->currentItem();QVERIFY(item);
            QCOMPARE(item->data(0,Qt::UserRole+1).toString(),"m-"+method.key);
            for(const auto &parameter:method.parameters){
                if(parameter.editorHidden)continue;
                const auto topic=parameterHelpTopic(method.key,parameter.key);
                QVERIFY(dialog.navigate(topic));item=tree->currentItem();QVERIFY(item);
                QCOMPARE(item->data(0,Qt::UserRole).toString(),topic.path);
                QCOMPARE(item->data(0,Qt::UserRole+1).toString(),topic.anchor);
                for(auto *parent=item->parent();parent;parent=parent->parent())QVERIFY(parent->isExpanded());
            }
        }
        const auto pages=helpNavigation(lang);
        QSet<QString> paths;
        for(const auto &page:pages){
            QVERIFY(!paths.contains(page.path));paths.insert(page.path);
            QVERIFY2(localHelpAvailable({page.path,{}}),qPrintable(page.path));
            for(const auto &section:helpSections(page.path))
                for(const auto &anchor:section.anchors)QVERIFY2(localHelpAvailable({page.path,anchor}),qPrintable(page.path+'#'+anchor));
        }
        QVERIFY(paths.contains(lang=="zh"?"docs/concepts/spectral_parameters.md":"docs/concepts/spectral_parameters_en.md"));
        QVERIFY(!paths.contains(lang=="zh"?"docs/concepts/spectral_parameters_en.md":"docs/concepts/spectral_parameters.md"));
        auto *group=tree->topLevelItem(1);tree->setCurrentItem(group);group->setExpanded(false);
        tree->setFocus();QTest::keyClick(tree,Qt::Key_Right);QVERIFY(group->isExpanded());
        QTest::keyClick(tree,Qt::Key_Left);QVERIFY(!group->isExpanded());
    }
}
void UiTests::helpGlobalSearchAndCancellation()
{
    struct Restore{~Restore(){setLanguage("zh");}} restore;
    setLanguage("zh");auto *dialog=new HelpDialog;dialog->show();
    QVERIFY(dialog->navigate(userGuideTopic()));
    auto *search=dialog->findChild<QLineEdit*>("helpGlobalSearch");
    auto *results=dialog->findChild<QListWidget*>("helpSearchResults");
    auto *status=dialog->findChild<QLabel*>("helpSearchStatus");QVERIFY(search&&results&&status);
    int heartbeats=0;QTimer timer;timer.setInterval(1);connect(&timer,&QTimer::timeout,[&]{++heartbeats;});timer.start();
    QElapsedTimer elapsed;elapsed.start();search->setText("evo_target_windows");
    QTRY_VERIFY_WITH_TIMEOUT(results->count()>0 && !status->text().contains(text("help.indexing").section('%',0,0)),15000);
    QVERIFY(heartbeats>0);qInfo()<<"Help cold search milliseconds"<<elapsed.elapsed();
    search->setFocus();QTest::keyClick(search,Qt::Key_Return);
    QVERIFY(dialog->currentTopic().anchor.contains("evo_target_windows"));
    const auto before=dialog->currentTopic();search->setText("__not_in_any_manual_89__");
    QTRY_COMPARE(status->text(),text("help.no_results"));QCOMPARE(results->count(),0);
    QCOMPARE(dialog->currentTopic().anchor,before.anchor);
    search->setText("NyQuIsT");QTRY_VERIFY(results->count()>0); // case-insensitive body text
    search->setText("频率 分辨");QTest::qWait(180);QTRY_VERIFY(status->text()!=text("help.no_results") && results->count()>0);
    search->setText("Nyquist __absent_second_term__");
    QTRY_COMPARE(status->text(),text("help.no_results"));QCOMPARE(results->count(),0);
    search->clear();QTRY_VERIFY(dialog->findChild<QTreeWidget*>("helpContents")->isVisible());
    QVERIFY(!status->isVisible());
    // Changing language rebuilds the catalogue and cancels old-query state.
    setLanguage("en");QVERIFY(dialog->navigate(userGuideTopic()));QVERIFY(search->text().isEmpty());
    search->setText("padding");QTRY_VERIFY_WITH_TIMEOUT(results->count()>0,15000);
    QTest::mouseClick(results->viewport(),Qt::LeftButton,Qt::NoModifier,results->visualItemRect(results->item(0)).center());
    QVERIFY(dialog->currentTopic().path.contains("_en",Qt::CaseInsensitive));
    auto *tree=dialog->findChild<QTreeWidget*>("helpContents");QVERIFY(tree->currentItem());
    search->clear();QVERIFY(tree->isVisible());
    const auto captures=qEnvironmentVariable("ASTER_HELP_CAPTURE_DIR");
    if(!captures.isEmpty()){
        QDir().mkpath(captures);search->setText("FFT");QTRY_VERIFY(results->count()>0);
        dialog->grab().save(captures+"/help-global-search-en.png");
    }
    QPointer<HelpDialog> guard(dialog);dialog->close();QTRY_VERIFY(guard.isNull());
    // Close during a pending index build: timers must not retain the dialog.
    auto *pending=new HelpDialog;pending->show();
    pending->findChild<QLineEdit*>("helpGlobalSearch")->setText("窗口");
    QPointer<HelpDialog> pendingGuard(pending);pending->close();QTRY_VERIFY(pendingGuard.isNull());
    QTest::qWait(150);
}
void UiTests::helpSearchHighlightsFollowTheArticle()
{
    const auto restore = qScopeGuard([] { setLanguage("zh"); ThemeManager::instance().apply(*qApp, "light"); });
    setLanguage("zh");
    auto *dialog = new HelpDialog;
    dialog->show();
    QVERIFY(dialog->navigate({"docs/README.md", {}}));
    auto *browser = dialog->findChild<QTextBrowser *>("helpBrowser");
    auto *global = dialog->findChild<QLineEdit *>("helpGlobalSearch");
    auto *local = dialog->findChild<QLineEdit *>("helpSearch");
    auto *results = dialog->findChild<QListWidget *>("helpSearchResults");
    const auto marks = [&] {
        QStringList words;
        for (auto block = browser->document()->begin(); block.isValid(); block = block.next())
            for (const auto &range : block.layout()->formats())
                if (range.format.background().color() == themeColor("helpMatch"))
                    words << block.text().mid(range.start, range.length);
        return words;
    };
    const auto original = browser->document()->toHtml();
    global->setText("分类");
    QTRY_VERIFY_WITH_TIMEOUT(marks().contains("分类"), 15000);
    QCOMPARE(browser->document()->toHtml(), original);
    auto *indexHit = results->findItems("文档索引", Qt::MatchStartsWith).value(0);
    QVERIFY(indexHit);
    QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier, results->visualItemRect(indexHit).center());
    QCOMPARE(dialog->currentTopic().path, QString("docs/README.md"));
    QVERIFY(marks().contains("分类"));
    ThemeManager::instance().apply(*qApp, "dark");
    QVERIFY(marks().contains("分类"));
    QCoreApplication::processEvents(); // Finish navigation to the clicked section.
    browser->verticalScrollBar()->setValue(42);
    const int scroll = browser->verticalScrollBar()->value();
    global->setText("左侧 搜索");
    QVERIFY(marks().isEmpty());
    QTRY_VERIFY(marks().contains("左侧") && marks().contains("搜索"));
    QCOMPARE(browser->verticalScrollBar()->value(), scroll);
    local->setText("浏览");
    QTest::keyClick(local, Qt::Key_Return);
    QCOMPARE(browser->textCursor().selectedText(), QString("浏览"));
    global->clear();
    QVERIFY(marks().isEmpty());
    QCOMPARE(browser->textCursor().selectedText(), QString("浏览"));
    global->setText("分类");
    QTRY_VERIFY(marks().contains("分类"));
    dialog->findChild<QPushButton *>("helpFindClose")->click();
    QVERIFY(!browser->textCursor().hasSelection());
    QVERIFY(marks().contains("分类"));

    QVERIFY(dialog->navigate({"LICENSE", {}}));
    QVERIFY(marks().isEmpty());
    dialog->findChild<QPushButton *>("helpBack")->click();
    QTRY_VERIFY(marks().contains("分类"));
    global->setText("分类 __no_matching_section__");
    QVERIFY(marks().isEmpty());
    QTRY_COMPARE(dialog->findChild<QLabel *>("helpSearchStatus")->text(), text("help.no_results"));
    QVERIFY(marks().isEmpty());
    global->setText("aStRoBaTcH");
    QTest::keyClick(global, Qt::Key_Return);
    QTRY_VERIFY(marks().contains("AstroBatch", Qt::CaseInsensitive));
    setLanguage("en");
    QVERIFY(dialog->navigate(userGuideTopic()));
    QVERIFY(global->text().isEmpty());
    QVERIFY(marks().isEmpty());
    global->setText("sampling");
    QPointer<HelpDialog> guard(dialog);
    dialog->close();
    QTRY_VERIFY(guard.isNull());
    HelpDialog reopened;
    QVERIFY(reopened.navigate(userGuideTopic()));
    QVERIFY(reopened.findChild<QLineEdit *>("helpGlobalSearch")->text().isEmpty());
    QVERIFY(reopened.findChild<QTextBrowser *>("helpBrowser")->document()->begin().layout()->formats().isEmpty());
}

void UiTests::hoverCardsRenderLiteralFileDetails()
{
    const auto restore = qScopeGuard([] { QToolTip::hideText(); setLanguage("zh"); ThemeManager::instance().apply(*qApp, "light"); });
    setLanguage("zh");
    QListWidget list;
    list.resize(350, 250);
    const QString name = "82107<&>.csv";
    const QString source = "C:/归档/" + QString(140, 'x') + "/" + name;
    const QString metadata = "ID 4247 · 2026-11-19 11:26";
    auto *item = new FileListItem(name, &list, {source, metadata, "<a href='test'>literal</a>", true},
                                  RecordItemDelegate::statusFor("PartialFailed"));
    list.show();
    list.activateWindow();
    QCoreApplication::processEvents();
    for (const auto *theme : {"light", "dark"}) {
        ThemeManager::instance().apply(*qApp, theme);
        const auto at = list.visualItemRect(item).center();
        QTest::mouseMove(list.viewport(), at);
        QCoreApplication::processEvents();
        QHelpEvent event(QEvent::ToolTip, at, list.viewport()->mapToGlobal(at));
        QApplication::sendEvent(list.viewport(), &event);
        const QString html = QToolTip::text();
        QVERIFY(html.contains(themeColor("error").name()));
        QTextDocument document;
        document.setHtml(html);
        const auto body = document.toPlainText().remove('\n');
        QVERIFY(body.startsWith(name));
        QVERIFY(body.contains(source));
        QVERIFY(body.contains(metadata));
        QVERIFY(body.contains(text("ui.record_partial_failure")));
        QVERIFY(body.contains("<a href='test'>literal</a>"));
        QVERIFY(body.contains(text("ui.record_workflow_error")));
        QVERIFY(document.find(name).charFormat().font().bold());
        QWidget *tip = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->isVisible() && widget->windowType() == Qt::ToolTip) tip = widget;
        QVERIFY(tip);
        QVERIFY(tip->width() < 460);
    }
}

void UiTests::stepStatusRetainsColorWhenSelected()
{
    const auto restore = qScopeGuard([] { setLanguage("zh"); ThemeManager::instance().apply(*qApp, "light"); });
    setLanguage("zh");
    QTemporaryDir directory;
    const auto path = directory.filePath("steps.sqlite");
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("sine_source", {0, 0}, {{"count", 8}}).nodeId;
    const auto failed = model.addChild(root, ChildSlot::Append, "affine").nodeId;
    const auto skipped = model.addChild(failed, ChildSlot::Append, "affine").nodeId;
    auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {"82107.csv", {}, {}}});
    run.state = RunState::PartialFailed;
    run.nodes[failed] = NodeResult::failure("upstream fixture");
    auto blocked = NodeResult::failure(makeError(ErrorCode::UpstreamFailed));
    blocked.state = NodeState::Blocked;
    run.nodes[skipped] = blocked;
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    const auto saved = repo.save(run);
    QVERIFY(saved.saved());
    ResultsPage page(makeScientificRegistry());
    page.resize(1100, 800);
    page.show();
    page.setDatabase(path);
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(saved.recordId);
    QTRY_COMPARE(loaded.count(), 1);
    auto *steps = page.findChild<QTableWidget *>("resultSteps");
    QCOMPARE(steps->rowCount(), 3);
    QCOMPARE(steps->item(1, 0)->text(), QString("失败"));
    QCOMPARE(steps->item(2, 0)->text(), QString("跳过"));
    QVERIFY(steps->item(2, 0)->toolTip() != steps->item(2, 0)->text());
    for (const auto *theme : {"light", "dark"}) {
        ThemeManager::instance().apply(*qApp, theme);
        for (int row = 0; row < 3; ++row) {
            const auto rect = steps->visualItemRect(steps->item(row, 0));
            QTest::mouseClick(steps->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
            QCOMPARE(steps->currentRow(), row);
            const auto expected = themeColor(row == 0 ? "success" : row == 1 ? "error" : "muted");
            const auto pixels = steps->viewport()->grab(rect).toImage();
            bool hasInk = false;
            for (int y = 0; y < pixels.height() && !hasInk; ++y)
                for (int x = 0; x < pixels.width(); ++x) {
                    const auto c = pixels.pixelColor(x, y);
                    if (qAbs(c.red() - expected.red()) + qAbs(c.green() - expected.green()) +
                        qAbs(c.blue() - expected.blue()) < 45) { hasInk = true; break; }
                }
            QVERIFY2(hasInk, qPrintable(QString("%1 row %2").arg(theme).arg(row)));
            const auto background = viewColorAt(steps->viewport(), {rect.right()-4, rect.center().y()});
            const auto expectedFill = themeColor("selection");
            // Native keyboard focus can slightly tint the current cell within the selected row.
            QVERIFY(qAbs(background.red() - expectedFill.red()) <= 6 &&
                    qAbs(background.green() - expectedFill.green()) <= 6 &&
                    qAbs(background.blue() - expectedFill.blue()) <= 6);
        }
    }
    QCOMPARE(stateLabel(NodeState::Cancelled), text("ui.cancelled"));
    setLanguage("en");
    QCOMPARE(stateLabel(NodeState::Blocked), QString("Skipped"));
}

void UiTests::helpFindAndReadingHistory()
{
    setLanguage("zh");HelpDialog dialog;dialog.show();QVERIFY(dialog.navigate(userGuideTopic("database")));
    auto *browser=dialog.findChild<QTextBrowser*>("helpBrowser");
    auto *search=dialog.findChild<QLineEdit*>("helpSearch");
    QTRY_VERIFY(browser->verticalScrollBar()->value()>0);
    browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->value()+37);
    const int position=browser->verticalScrollBar()->value();
    QVERIFY(dialog.navigate(parameterHelpTopic("mtm","smooth_window")));
    QTRY_VERIFY(browser->verticalScrollBar()->value()>0);
    dialog.findChild<QPushButton*>("helpBack")->click();QTRY_COMPARE(browser->verticalScrollBar()->value(),position);
    dialog.findChild<QPushButton*>("helpForward")->click();QTRY_VERIFY(browser->verticalScrollBar()->value()>0);
    QCOMPARE(dialog.currentTopic().anchor,QString("m-mtm-p-smooth_window"));
    dialog.findChild<QPushButton*>("helpBack")->click();QTRY_COMPARE(browser->verticalScrollBar()->value(),position);
    QVERIFY(dialog.navigate({"FAQ.md",{}}));QVERIFY(!dialog.findChild<QPushButton*>("helpForward")->isEnabled());
    dialog.findChild<QPushButton*>("helpFindToggle")->click();QVERIFY(search->isVisible());
    search->setText("Windows");QTest::keyClick(search,Qt::Key_Return);
    QCOMPARE(browser->textCursor().selectedText(),QString("Windows"));
    const auto first=browser->textCursor().selectionStart();
    dialog.findChild<QPushButton*>("helpFindPrevious")->click();
    QCOMPARE(browser->textCursor().selectedText(),QString("Windows"));
    dialog.findChild<QPushButton*>("helpFindNext")->click();QCOMPARE(browser->textCursor().selectionStart(),first);
    search->setText("__no_match__");QTest::keyClick(search,Qt::Key_Return);
    QCOMPARE(dialog.findChild<QLabel*>("helpFindStatus")->text(),text("help.no_match"));
    dialog.findChild<QPushButton*>("helpFindClose")->click();QVERIFY(!search->isVisible());
    dialog.activateWindow();browser->setFocus();QTest::keyClick(browser,Qt::Key_F,Qt::ControlModifier);
    QTRY_VERIFY(search->isVisible());
    search->setText("Windows");QTest::keyClick(search,Qt::Key_Return);QVERIFY(dialog.isVisible());
    browser->setPlainText(QString::fromUtf8("A 🪐B éZ 🪐B"));
    search->setText(QString::fromUtf8("🪐B"));QTest::keyClick(search,Qt::Key_Return);
    QCOMPARE(browser->textCursor().selectedText(),QString::fromUtf8("🪐B"));
    search->setText(QString::fromUtf8("é"));QTest::keyClick(search,Qt::Key_Return);
    QCOMPARE(browser->textCursor().selectedText(),QString::fromUtf8("é"));
}
void UiTests::helpNavigationResponsiveLayout()
{
    struct Restore{~Restore(){setLanguage("zh");ThemeManager::instance().apply(*qApp,"light");}} restore;
    const auto captures=qEnvironmentVariable("ASTER_HELP_CAPTURE_DIR");
    for(const auto &lang:QStringList{"zh","en"})for(const auto &theme:QStringList{"light","dark"}){
        setLanguage(lang);ThemeManager::instance().apply(*qApp,theme);
        HelpDialog dialog;dialog.resize(900,640);dialog.show();
        QVERIFY(dialog.navigate({lang=="zh"?"docs/concepts/spectral_parameters.md":"docs/concepts/spectral_parameters_en.md","fft-padding"}));
        auto *browser=dialog.findChild<QTextBrowser*>("helpBrowser");QTRY_VERIFY(browser->verticalScrollBar()->value()>0);
        auto *tree=dialog.findChild<QTreeWidget*>("helpContents");QVERIFY(tree->currentItem());
        QVERIFY(dialog.findChild<QLabel*>("helpBreadcrumb")->text().contains(lang=="zh"?"通用":"Shared"));
        QVERIFY(browser->width()>400);QVERIFY(tree->width()>=180);
        dialog.findChild<QPushButton*>("helpFindToggle")->click();
        QTest::qWait(20);
        for(auto *widget:{static_cast<QWidget*>(browser),static_cast<QWidget*>(tree),dialog.findChild<QWidget*>("helpFindBar")}){
            const auto topLeft=widget->mapTo(&dialog,QPoint());
            QVERIFY(topLeft.x()>=0 && topLeft.x()+widget->width()<=dialog.width());
        }
        if(!captures.isEmpty()){QDir().mkpath(captures);dialog.grab().save(captures+"/help-navigation-"+lang+"-"+theme+".png");}
        dialog.resize(680,480);QTest::qWait(20);QVERIFY(browser->width()>=280);
    }
}

void UiTests::helpLinksAndFallback()
{
    auto *dialog = new HelpDialog;
    QPointer<HelpDialog> guarded(dialog);
    dialog->show();
    QVERIFY(dialog->navigate(parameterHelpTopic("affine", "factor")));
    auto *browser = dialog->findChild<QTextBrowser *>("helpBrowser");
    QVERIFY(browser);
    for (auto *button : dialog->findChildren<QPushButton *>())
    {
        QVERIFY(!button->text().startsWith("ui."));
        QVERIFY(!button->text().startsWith("help."));
    }
    QTRY_VERIFY(browser->verticalScrollBar()->value() > 0);
    QVERIFY(browser->toPlainText().contains(QString::fromUtf8("缩放系数")));
    QVERIFY(!browser->toPlainText().contains("parameterhelp.factor"));
    // The requested heading is in the viewport, not merely present somewhere in the document.
    auto cursor = browser->document()->find(QString::fromUtf8("缩放系数"));
    QVERIFY(!cursor.isNull());
    const QRect heading = browser->cursorRect(cursor);
    QVERIFY(heading.bottom() >= 0 && heading.top() < browser->viewport()->height());
    QVERIFY(dialog->navigate(userGuideTopic("database")));
    QTRY_VERIFY(browser->verticalScrollBar()->value() > 0);
    dialog->findChild<QPushButton *>("helpBack")->click();
    QCOMPARE(dialog->currentTopic().anchor, QString("m-affine-p-factor"));
    dialog->findChild<QPushButton *>("helpForward")->click();
    QCOMPARE(dialog->currentTopic().anchor, QString("database"));
    // Relative paths and fragments use the current local document as their base.
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
                                     Q_ARG(QUrl, QUrl("../FAQ.md#linux-fonts"))));
    QCOMPARE(dialog->currentTopic().path, QString("FAQ.md"));
    QCOMPARE(dialog->currentTopic().anchor, QString("linux-fonts"));
    QVERIFY(QMetaObject::invokeMethod(browser,"anchorClicked",Qt::DirectConnection,
        Q_ARG(QUrl,QUrl("https://gitee.com/ChenCLian/AstroBatch/blob/main/docs/USER_GUIDE.md#database"))));
    QCOMPARE(dialog->currentTopic().path,QString("docs/USER_GUIDE.md"));
    QCOMPARE(dialog->currentTopic().anchor,QString("database"));
    QCOMPARE(onlineHelpUrl({"FAQ.md","linux-fonts"},"gitee.com").toString(),QString("https://gitee.com/ChenCLian/AstroBatch/blob/main/FAQ.md#linux-fonts"));
    QVERIFY(onlineHelpUrl({"FAQ.md",{}},"evil.invalid").isEmpty());
    QVERIFY(dialog->navigate(userGuideTopic()));
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
                                     Q_ARG(QUrl, QUrl("#database"))));
    QCOMPARE(dialog->currentTopic().anchor, QString("database"));
    for (const auto &theme : {QString("dark"), QString("light")})
    {
        ThemeManager::instance().apply(*qApp, theme);
        int linkCount = 0;
        for (auto block = browser->document()->begin(); block.isValid(); block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it)
            {
                const auto fragment = it.fragment();
                if (fragment.isValid() && !fragment.charFormat().anchorHref().isEmpty())
                {
                    ++linkCount;
                    QCOMPARE(fragment.charFormat().foreground().color(), themeColor("accent"));
                    QVERIFY(fragment.charFormat().fontUnderline());
                }
            }
        QVERIFY(linkCount > 0);
    }
    auto *search = dialog->findChild<QLineEdit *>("helpSearch");
    search->setText(QString::fromUtf8("SQLite")); search->setFocus();
    QTest::keyClick(search, Qt::Key_Return);
    QVERIFY(dialog->isVisible());
    QCOMPARE(browser->textCursor().selectedText(), QString("SQLite"));
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct ResetHandler { ~ResetHandler() { QDesktopServices::unsetUrlHandler("https"); } } reset;
    const HelpTopic missing{"docs/missing-manual.md", "missing-section"};
    QVERIFY(!dialog->navigate(missing));
    QCOMPARE(capturedHelpUrls.size(), 1);
    QCOMPARE(capturedHelpUrls.last().toString(), QString("https://github.com/ChenCLian/AstroBatch/blob/main/docs/missing-manual.md#missing-section"));
    QVERIFY(!dialog->navigate({"docs/USER_GUIDE.md", "missing-section"}));
    QCOMPARE(capturedHelpUrls.size(), 2);
    QVERIFY(!localHelpAvailable({"../FAQ.md", {}}));
    QVERIFY(onlineHelpUrl({"../FAQ.md", {}}).isEmpty());
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
                                     Q_ARG(QUrl, QUrl("file:///etc/passwd"))));
    QCOMPARE(capturedHelpUrls.size(), 2);
    const QString captures = qEnvironmentVariable("ASTER_HELP_CAPTURE_DIR");
    if (!captures.isEmpty())
    {
        QDir().mkpath(captures);
        QVERIFY(dialog->navigate(parameterHelpTopic("mtm", "smooth_window")));
        QTest::qWait(100);
        QVERIFY(dialog->grab().save(captures + "/help-light.png"));
        ThemeManager::instance().apply(*qApp, "dark");
        QTest::qWait(100);
        QVERIFY(dialog->grab().save(captures + "/help-dark.png"));
        ThemeManager::instance().apply(*qApp, "light");
    }
    dialog->navigate(userGuideTopic("database"));
    dialog->close(); // Pending anchor timer must not outlive the viewer.
    QTRY_VERIFY(guarded.isNull());
}

void UiTests::helpLanguageAndChapterReuse()
{
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct ResetHandler {
        ~ResetHandler() { QDesktopServices::unsetUrlHandler("https"); setLanguage("zh"); }
    } reset;
    auto *dialog = new HelpDialog;
    dialog->show();
    auto *browser = dialog->findChild<QTextBrowser *>("helpBrowser");
    QVERIFY(browser);
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        const QString path = language == "zh" ? "docs/reference/task_parameters.md"
                                               : "docs/reference/task_parameters_en.md";
        const auto topic = parameterHelpTopic("affine", "factor");
        QCOMPARE(topic.path, path);
        QVERIFY(dialog->navigate(userGuideTopic()));
        const QString relative = language == "zh" ? "reference/task_parameters.md#m-affine-p-factor"
                                                   : "reference/task_parameters_en.md#m-affine-p-factor";
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
                                         Q_ARG(QUrl, QUrl(relative))));
        QCOMPARE(dialog->currentTopic().path, topic.path);
        QCOMPARE(dialog->currentTopic().anchor, topic.anchor);
        QTRY_VERIFY(browser->verticalScrollBar()->value() > 0);
        const QString content = browser->toPlainText();
        QVERIFY(content.contains(language == "zh" ? QString::fromUtf8("默认值:") : "Default:"));
        QVERIFY(!content.contains(language == "zh" ? "Default:" : QString::fromUtf8("默认值:")));
        QVERIFY(!content.contains(" / Horizontal axis"));
        // Changing a parameter, going Back/Forward or following an anchor in
        // this document must not replace its text or repeat layout work.
        QSignalSpy changed(browser->document(), &QTextDocument::contentsChanged);
        QVERIFY(dialog->navigate(parameterHelpTopic("affine", "offset")));
        QTRY_VERIFY(browser->verticalScrollBar()->value() > 0);
        QCOMPARE(changed.count(), 0);
        dialog->findChild<QPushButton *>("helpBack")->click();
        QCOMPARE(dialog->currentTopic().anchor, QString("m-affine-p-factor"));
        dialog->findChild<QPushButton *>("helpForward")->click();
        QCOMPARE(dialog->currentTopic().anchor, QString("m-affine-p-offset"));
        QCOMPARE(changed.count(), 0);
        const auto url = onlineHelpUrl(topic);
        QVERIFY(url.path().endsWith(path));
        QCOMPARE(url.fragment(), topic.anchor);
        // The same GitHub URL is also resolved locally if available.
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
                                         Q_ARG(QUrl, url)));
        QCOMPARE(dialog->currentTopic().path, path);
        QCOMPARE(changed.count(), 0);
        const auto before = dialog->currentTopic();
        QVERIFY(!dialog->navigate({path, "missing-parameter"}));
        QCOMPARE(capturedHelpUrls.last().path(), url.path());
        QCOMPARE(capturedHelpUrls.last().fragment(), QString("missing-parameter"));
        QCOMPARE(dialog->currentTopic().anchor, before.anchor);
        QCOMPARE(browser->toPlainText(), content);
    }
    QPointer<HelpDialog> previous(dialog);
    dialog->close();
    QTRY_VERIFY(previous.isNull());
    // A newly opened reference retains the selected language after closing.
    auto *reopened = new HelpDialog;
    QVERIFY(reopened->navigate(methodHelpTopic("mtm")));
    QCOMPARE(reopened->currentTopic().path, QString("docs/reference/task_parameters_en.md"));
    QVERIFY(!reopened->findChild<QTextBrowser *>("helpBrowser")->toPlainText().contains(QString::fromUtf8("默认值:")));
    delete reopened;
}

void UiTests::helpReferenceVisibleLayout()
{
    struct Restore {
        ~Restore() { setLanguage("zh"); ThemeManager::instance().apply(*qApp, "light"); }
    } restore;
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        auto *dialog = new HelpDialog;
        dialog->show();
        auto *browser = dialog->findChild<QTextBrowser *>("helpBrowser");
        QVERIFY(dialog->navigate(methodHelpTopic("affine")));
        for (const auto &theme : {QString("light"), QString("dark")})
        {
            ThemeManager::instance().apply(*qApp, theme);
            for (int i = 0; i < 12; ++i)
                QVERIFY(dialog->navigate(parameterHelpTopic("affine", i % 2 ? "factor" : "offset")));
            QVERIFY(dialog->navigate(methodHelpTopic("odp846_d13c")));
            QTest::qWait(30);
            const QString title = language == "zh" ? QString::fromUtf8("ODP 846 · 碳同位素 δ13C")
                                                     : QString::fromUtf8("ODP 846 · carbon isotopes δ13C");
            auto *document = browser->document();
            QTextBlock section;
            for (auto block = document->begin(); block.isValid(); block = block.next())
                if (block.blockFormat().headingLevel() == 2 && block.text() == title)
                { section = block; break; }
            QVERIFY(section.isValid());
            int visibleBlocks = 0;
            for (auto block = section; block.isValid(); block = block.next())
            {
                if (block != section && block.blockFormat().headingLevel() == 2) break;
                if (block.text().trimmed().isEmpty()) continue;
                QVERIFY2(document->documentLayout()->blockBoundingRect(block).height() > 0,
                         qPrintable(language + '/' + theme + ": " + block.text()));
                ++visibleBlocks;
            }
            QVERIFY(visibleBlocks >= 7); // Title, method, parameter, help, default, choices, output.
            const QRect titleRect = browser->cursorRect(QTextCursor(section));
            QVERIFY(titleRect.bottom() >= 0 && titleRect.top() < browser->viewport()->height());
        }
        delete dialog;
    }
}

void UiTests::samplingGuidesStayOfflineAndReturnToParameter()
{
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct Restore {
        ~Restore() { QDesktopServices::unsetUrlHandler("https"); setLanguage("zh"); }
    } restore;
    HelpDialog dialog;
    auto *browser = dialog.findChild<QTextBrowser *>("helpBrowser");
    QVERIFY(browser);
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        const QString suffix = language == "zh" ? "" : "_en";
        const QString conceptPath = "docs/concepts/sampling" + suffix + ".md";
        const QString taskPath = "docs/methods/preprocessing" + suffix + ".md";
        for (const auto &anchor : {"coordinates", "duplicates", "spacing", "interpolation", "gaps", "limits"})
            QVERIFY(localHelpAvailable({conceptPath, anchor}));
        for (const auto &anchor : {"inspection", "inspection-output", "cleanup", "resampling", "grid-endpoints", "resampling-failures"})
            QVERIFY(localHelpAvailable({taskPath, anchor}));
        for (const auto &method : {QString("clean_series"), QString("preprocess")})
        {
            const auto origin = parameterHelpTopic(method, "duplicate_policy");
            QVERIFY(dialog.navigate(origin));
            const QString href = "../concepts/sampling" + suffix + ".md#duplicates";
            QVERIFY(browser->document()->toHtml().contains(href));
            QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
                                             Q_ARG(QUrl, QUrl(href))));
            QCOMPARE(dialog.currentTopic().path, conceptPath);
            QCOMPARE(dialog.currentTopic().anchor, QString("duplicates"));
            dialog.findChild<QPushButton *>("helpBack")->click();
            QCOMPARE(dialog.currentTopic().path, origin.path);
            QCOMPARE(dialog.currentTopic().anchor, origin.anchor);
            dialog.findChild<QPushButton *>("helpForward")->click();
            QCOMPARE(dialog.currentTopic().path, conceptPath);
        }
        QVERIFY(dialog.navigate({taskPath, "inspection-output"}));
        QVERIFY(browser->toPlainText().contains("4096"));
        QVERIFY(browser->toPlainText().contains("interval_details_truncated"));
        const auto captures = qEnvironmentVariable("ASTER_HELP_CAPTURE_DIR");
        if (!captures.isEmpty())
        {
            QDir().mkpath(captures);
            dialog.resize(900, 680);
            dialog.show();
            QTest::qWait(30);
            QVERIFY(dialog.grab().save(captures + "/sampling-" + language + ".png"));
        }
        QVERIFY(dialog.navigate({"docs/ui/live_preview.md", "display-limits"}));
    }
    QVERIFY(capturedHelpUrls.isEmpty());
}

void UiTests::spectralGuidesStayOfflineAndShareConcepts()
{
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct Restore {
        ~Restore() { QDesktopServices::unsetUrlHandler("https"); setLanguage("zh"); }
    } restore;
    HelpDialog dialog;
    auto *browser = dialog.findChild<QTextBrowser *>("helpBrowser");
    QVERIFY(browser);
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        const QString suffix = language == "zh" ? "" : "_en";
        const QString conceptPath = "docs/concepts/spectral_parameters" + suffix + ".md";
        const QString taskPath = "docs/methods/power_spectra" + suffix + ".md";
        for (const auto &anchor : {"windows", "overlap", "taper-trend", "fft-padding", "frequency", "power-units", "normalization", "display"})
            QVERIFY(localHelpAvailable({conceptPath, anchor}));
        for (const auto &anchor : {"fft", "welch", "evolution-settings", "evolution-output", "preset", "limits"})
            QVERIFY(localHelpAvailable({taskPath, anchor}));
        for (const auto &method : {QString("fft_spectrum"), QString("welch_spectrum"), QString("evolution_fft"), QString("evolution_mtm")})
        {
            const QString parameter = method.startsWith("evolution") ? "evo_nfft" : "psd_nfft";
            const auto origin = parameterHelpTopic(method, parameter);
            QVERIFY(dialog.navigate(origin));
            const QString href = "../concepts/spectral_parameters" + suffix + ".md#fft-padding";
            QVERIFY(browser->document()->toHtml().contains(href));
            QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection, Q_ARG(QUrl, QUrl(href))));
            QCOMPARE(dialog.currentTopic().path, conceptPath);
            QCOMPARE(dialog.currentTopic().anchor, QString("fft-padding"));
            dialog.findChild<QPushButton *>("helpBack")->click();
            QCOMPARE(dialog.currentTopic().path, origin.path);
            QCOMPARE(dialog.currentTopic().anchor, origin.anchor);
        }
    }
    QVERIFY(capturedHelpUrls.isEmpty());
}

void UiTests::dataProcessingGuidesStayOfflineAndReturn()
{
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct Restore {
        ~Restore() { QDesktopServices::unsetUrlHandler("https"); setLanguage("zh"); }
    } restore;
    HelpDialog dialog;
    auto *browser = dialog.findChild<QTextBrowser *>("helpBrowser");
    QVERIFY(browser);
    struct Topic { const char *method; const char *path; const char *anchor; };
    const Topic topics[] = {
        {"affine", "concepts/data_processing", "centering"},
        {"demean", "concepts/data_processing", "centering"},
        {"standardize", "concepts/data_processing", "centering"},
        {"moving_average", "methods/smoothing_trends", "moving-average"},
        {"savitzky_golay", "methods/smoothing_trends", "savitzky-golay"},
        {"linear_detrend", "methods/smoothing_trends", "linear"},
        {"detrend_lowess", "methods/smoothing_trends", "lowess"},
        {"outlier_handling", "methods/outliers_and_transforms", "outliers"},
        {"value_transform", "methods/outliers_and_transforms", "transforms"},
        {"butterworth_lowpass", "methods/filter_usage", "butterworth"},
        {"butterworth_highpass", "methods/filter_usage", "butterworth"},
        {"gaussian_filter", "methods/filter_usage", "bandpass"},
        {"taner_filter", "methods/filter_usage", "bandpass"},
        {"hilbert_envelope", "methods/filter_usage", "hilbert"}
    };
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        const QString suffix = language == "zh" ? "" : "_en";
        for (const auto &t : topics)
        {
            const auto origin = methodHelpTopic(t.method);
            QVERIFY(dialog.navigate(origin));
            const QString path = QString(t.path) + suffix + ".md";
            const QString href = "../" + path + "#" + t.anchor;
            QVERIFY2(browser->document()->toHtml().contains(href), t.method);
            QVERIFY(localHelpAvailable({"docs/" + path, t.anchor}));
            QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection, Q_ARG(QUrl, QUrl(href))));
            QCOMPARE(dialog.currentTopic().path, "docs/" + path);
            QCOMPARE(dialog.currentTopic().anchor, QString(t.anchor));
            dialog.findChild<QPushButton *>("helpBack")->click();
            QCOMPARE(dialog.currentTopic().path, origin.path);
            QCOMPARE(dialog.currentTopic().anchor, origin.anchor);
        }
        // Same parameter name has different domains in MTM and sample smoothing.
        for (const auto &method : {QString("moving_average"), QString("savitzky_golay")})
        {
            QVERIFY(dialog.navigate(parameterHelpTopic(method, "smooth_window")));
            QVERIFY(browser->document()->toHtml().contains("../concepts/data_processing" + suffix + ".md#neighborhoods"));
        }
        QVERIFY(dialog.navigate({"docs/methods/smoothing_trends" + suffix + ".md", "lowess"}));
        QVERIFY(browser->toPlainText().contains("mean(trend)"));
        QVERIFY(dialog.navigate({"docs/methods/outliers_and_transforms" + suffix + ".md", "audit"}));
        QVERIFY(browser->toPlainText().contains("result_value"));
    }
    QVERIFY(capturedHelpUrls.isEmpty());
}

void UiTests::statisticalGuidesStayOfflineAndReturn()
{
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct Restore {
        ~Restore() { QDesktopServices::unsetUrlHandler("https"); setLanguage("zh"); }
    } restore;
    HelpDialog dialog;
    auto *browser = dialog.findChild<QTextBrowser *>("helpBrowser");
    QVERIFY(browser);
    struct Topic { const char *method; const char *path; const char *anchor; };
    const Topic topics[] = {
        {"mtm", "methods/spectral_inference", "mtm"},
        {"lomb_scargle", "methods/spectral_inference", "lomb"},
        {"cross_spectrum", "methods/spectral_inference", "cross"},
        {"cwt", "methods/spectral_inference", "wavelet"},
        {"ar1_global", "methods/spectral_inference", "ar1"},
        {"mtm_ar1_simulation", "methods/spectral_inference", "legacy"}
    };
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        const QString suffix = language == "zh" ? "" : "_en";
        for (const auto &t : topics)
        {
            const auto origin = methodHelpTopic(t.method);
            QVERIFY(dialog.navigate(origin));
            const QString path = QString(t.path) + suffix + ".md";
            const QString href = "../" + path + "#" + t.anchor;
            QVERIFY2(browser->document()->toHtml().contains(href), t.method);
            QVERIFY(localHelpAvailable({"docs/" + path, t.anchor}));
            QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection, Q_ARG(QUrl, QUrl(href))));
            QCOMPARE(dialog.currentTopic().path, "docs/" + path);
            QCOMPARE(dialog.currentTopic().anchor, QString(t.anchor));
            dialog.findChild<QPushButton *>("helpBack")->click();
            QCOMPARE(dialog.currentTopic().path, origin.path);
            QCOMPARE(dialog.currentTopic().anchor, origin.anchor);
        }
        const QString concepts = "docs/concepts/statistical_interpretation" + suffix + ".md";
        for (const auto &anchor : {"outputs", "local-global", "fdr", "background", "unavailable", "coi"})
            QVERIFY(localHelpAvailable({concepts, anchor}));
        QVERIFY(dialog.navigate(parameterHelpTopic("mtm", "swa")));
        const QString href = "../concepts/statistical_interpretation" + suffix + ".md#fdr";
        QVERIFY(browser->document()->toHtml().contains(href));
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection, Q_ARG(QUrl, QUrl(href))));
        QCOMPARE(dialog.currentTopic().path, concepts);
        QCOMPARE(dialog.currentTopic().anchor, QString("fdr"));
        dialog.findChild<QPushButton *>("helpBack")->click();
        QCOMPARE(dialog.currentTopic().anchor, parameterHelpTopic("mtm", "swa").anchor);
    }
    QVERIFY(capturedHelpUrls.isEmpty());
}

void UiTests::chronologyGuidesStayOfflineAndReturn()
{
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct Restore {
        ~Restore() { QDesktopServices::unsetUrlHandler("https"); setLanguage("zh"); }
    } restore;
    HelpDialog dialog;
    auto *browser = dialog.findChild<QTextBrowser *>("helpBrowser");
    QVERIFY(browser);
    struct Topic { const char *method; const char *path; const char *anchor; };
    const Topic topics[] = {
        {"sedrate_to_age", "methods/chronology_usage", "rate"},
        {"apply_age_model", "methods/chronology_usage", "apply"},
        {"cycle_age_model", "methods/chronology_usage", "cycles"},
        {"milankovitch_period", "methods/chronology_usage", "periods"},
        {"coco_scan", "methods/coco_usage", "scan"},
        {"coco_monte_carlo", "methods/coco_usage", "monte-carlo"},
        {"evolution_coco", "methods/coco_usage", "evolution"},
        {"orbital_source", "methods/astronomy_usage", "orbital"},
        {"eccentricity_source", "methods/astronomy_usage", "orbital"},
        {"precession_tilt_source", "methods/astronomy_usage", "pt"},
        {"etp_source", "methods/astronomy_usage", "etp"},
        {"insolation_source", "methods/astronomy_usage", "daily"},
        {"seasonal_insolation", "methods/astronomy_usage", "season"},
        {"insolation_map", "methods/astronomy_usage", "map"}
    };
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        const QString suffix = language == "zh" ? "" : "_en";
        for (const auto &t : topics)
        {
            const auto origin = methodHelpTopic(t.method);
            QVERIFY(dialog.navigate(origin));
            const QString path = QString(t.path) + suffix + ".md";
            const QString href = "../" + path + "#" + t.anchor;
            QVERIFY2(browser->document()->toHtml().contains(href), t.method);
            QVERIFY(localHelpAvailable({"docs/" + path, t.anchor}));
            QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection, Q_ARG(QUrl, QUrl(href))));
            QCOMPARE(dialog.currentTopic().path, "docs/" + path);
            QCOMPARE(dialog.currentTopic().anchor, QString(t.anchor));
            dialog.findChild<QPushButton *>("helpBack")->click();
            QCOMPARE(dialog.currentTopic().path, origin.path);
            QCOMPARE(dialog.currentTopic().anchor, origin.anchor);
        }
        const QString concepts = "docs/concepts/chronology_coordinates" + suffix + ".md";
        for (const auto &anchor : {"coordinates", "units", "epoch", "coverage", "sampling", "files"})
            QVERIFY(localHelpAvailable({concepts, anchor}));
        // One shared concept is reached from multiple tasks, with return to each origin.
        for (const auto &method : {"sedrate_to_age", "apply_age_model", "cycle_age_model"})
        {
            const auto origin = parameterHelpTopic(method, "chrono_age_reference");
            QVERIFY(dialog.navigate(origin));
            const QString href = "../concepts/chronology_coordinates" + suffix + ".md#epoch";
            QVERIFY(browser->document()->toHtml().contains(href));
            QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection, Q_ARG(QUrl, QUrl(href))));
            QCOMPARE(dialog.currentTopic().path, concepts);
            QCOMPARE(dialog.currentTopic().anchor, QString("epoch"));
            dialog.findChild<QPushButton *>("helpBack")->click();
            QCOMPARE(dialog.currentTopic().path, origin.path);
            QCOMPARE(dialog.currentTopic().anchor, origin.anchor);
        }
    }
    QVERIFY(capturedHelpUrls.isEmpty());
}

void UiTests::allTaskGuidesAndIndexStayOffline()
{
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct Restore {
        ~Restore() { QDesktopServices::unsetUrlHandler("https"); setLanguage("zh"); }
    } restore;
    HelpDialog dialog;
    auto *browser = dialog.findChild<QTextBrowser *>("helpBrowser");
    QVERIFY(browser);
    struct Topic { const char *method; const char *path; const char *anchor; };
    const Topic topics[] = {
        {"affine", "concepts/data_processing", "centering"},
        {"apply_age_model", "methods/chronology_usage", "apply"},
        {"ar1_global", "methods/spectral_inference", "ar1"},
        {"butterworth_highpass", "methods/filter_usage", "butterworth"},
        {"butterworth_lowpass", "methods/filter_usage", "butterworth"},
        {"clean_series", "methods/preprocessing", "cleanup"},
        {"coco_monte_carlo", "methods/coco_usage", "monte-carlo"},
        {"coco_scan", "methods/coco_usage", "scan"},
        {"cross_spectrum", "methods/spectral_inference", "cross"},
        {"cwt", "methods/spectral_inference", "wavelet"},
        {"cycle_age_model", "methods/chronology_usage", "cycles"},
        {"database_grid", "methods/data_sources", "database"},
        {"database_series", "methods/data_sources", "database"},
        {"database_spectrum", "methods/data_sources", "database"},
        {"database_table", "methods/data_sources", "database"},
        {"demean", "concepts/data_processing", "centering"},
        {"detrend_lowess", "methods/smoothing_trends", "lowess"},
        {"eccentricity_source", "methods/astronomy_usage", "orbital"},
        {"etp_source", "methods/astronomy_usage", "etp"},
        {"evolution_coco", "methods/coco_usage", "evolution"},
        {"evolution_fft", "methods/power_spectra", "evolution-settings"},
        {"evolution_mtm", "methods/power_spectra", "evolution-settings"},
        {"fft_spectrum", "methods/power_spectra", "fft"},
        {"file_source", "methods/data_sources", "files"},
        {"gaussian_filter", "methods/filter_usage", "bandpass"},
        {"hilbert_envelope", "methods/filter_usage", "hilbert"},
        {"input_series", "methods/data_sources", "bound"},
        {"insolation_map", "methods/astronomy_usage", "map"},
        {"insolation_source", "methods/astronomy_usage", "daily"},
        {"linear_detrend", "methods/smoothing_trends", "linear"},
        {"lomb_scargle", "methods/spectral_inference", "lomb"},
        {"milankovitch_period", "methods/chronology_usage", "periods"},
        {"moving_average", "methods/smoothing_trends", "moving-average"},
        {"mtm", "methods/spectral_inference", "mtm"},
        {"mtm_ar1_simulation", "methods/spectral_inference", "legacy"},
        {"odp1143_d18o", "methods/data_sources", "odp"},
        {"odp846_d13c", "methods/data_sources", "odp"},
        {"odp846_d18o", "methods/data_sources", "odp"},
        {"odp926_d13c", "methods/data_sources", "odp"},
        {"odp926_lightness", "methods/data_sources", "odp"},
        {"orbital_source", "methods/astronomy_usage", "orbital"},
        {"outlier_handling", "methods/outliers_and_transforms", "outliers"},
        {"precession_tilt_source", "methods/astronomy_usage", "pt"},
        {"preprocess", "methods/preprocessing", "resampling"},
        {"sampling_check", "methods/preprocessing", "inspection"},
        {"savitzky_golay", "methods/smoothing_trends", "savitzky-golay"},
        {"seasonal_insolation", "methods/astronomy_usage", "season"},
        {"sedrate_to_age", "methods/chronology_usage", "rate"},
        {"select_interval", "methods/selection_summary", "interval"},
        {"series_summary", "methods/selection_summary", "summary"},
        {"sine_source", "methods/data_sources", "sine"},
        {"standardize", "concepts/data_processing", "centering"},
        {"synthetic_orbital", "methods/data_sources", "synthetic"},
        {"taner_filter", "methods/filter_usage", "bandpass"},
        {"value_transform", "methods/outliers_and_transforms", "transforms"},
        {"welch_spectrum", "methods/power_spectra", "welch"}
    };
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        const QString suffix = language == "zh" ? "" : "_en";
        for (const auto &t : topics)
        {
            const auto origin = methodHelpTopic(t.method);
            QVERIFY(dialog.navigate(origin));
            const QString path = QString(t.path) + suffix + ".md";
            const QString href = "../" + path + "#" + t.anchor;
            QVERIFY2(browser->document()->toHtml().contains(href), t.method);
            QVERIFY(localHelpAvailable({"docs/" + path, t.anchor}));
            QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection, Q_ARG(QUrl, QUrl(href))));
            QCOMPARE(dialog.currentTopic().path, "docs/" + path);
            QCOMPARE(dialog.currentTopic().anchor, QString(t.anchor));
            dialog.findChild<QPushButton *>("helpBack")->click();
            QCOMPARE(dialog.currentTopic().path, origin.path);
            QCOMPARE(dialog.currentTopic().anchor, origin.anchor);
        }
        const QString index = language == "zh" ? "docs/README.md" : "docs/README_EN.md";
        for (const auto &anchor : {"start", "features", "shared", "troubleshooting", "about"})
            QVERIFY(localHelpAvailable({index, anchor}));
        QVERIFY(dialog.navigate({index, "features"}));
        const QString href = "methods/data_sources" + suffix + ".md";
        QVERIFY(browser->document()->toHtml().contains(href));
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection, Q_ARG(QUrl, QUrl(href))));
        QCOMPARE(dialog.currentTopic().path, "docs/" + href);
        dialog.findChild<QPushButton *>("helpBack")->click();
        QCOMPARE(dialog.currentTopic().path, index);
        QCOMPARE(dialog.currentTopic().anchor, QString("features"));
        QVERIFY(localHelpAvailable({"resources/synthetic/NOTICE.md", {}}));
        QPushButton *home = nullptr;
        for (auto *button : dialog.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("主页") || button->text() == "Home") home = button;
        QVERIFY(home);
        home->click();
        QCOMPARE(dialog.currentTopic().path, userGuideTopic().path);
        QVERIFY(dialog.currentTopic().anchor.isEmpty());
    }
    QVERIFY(capturedHelpUrls.isEmpty());
}

void UiTests::helpDirectoryLinks()
{
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct ResetHandler { ~ResetHandler() { QDesktopServices::unsetUrlHandler("https"); } } reset;
    HelpDialog dialog;
    auto *browser = dialog.findChild<QTextBrowser *>("helpBrowser");
    QVERIFY(dialog.navigate({"AUTHORS.md", {}}));
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
                                     Q_ARG(QUrl, QUrl("licenses/"))));
    QCOMPARE(capturedHelpUrls.size(), 1);
    QCOMPARE(capturedHelpUrls.last().toString(), QString("https://github.com/ChenCLian/AstroBatch/tree/main/licenses"));
    QCOMPARE(dialog.currentTopic().path, QString("AUTHORS.md"));
    QVERIFY(localHelpAvailable({"docs/methods/", {}}));
    QVERIFY(dialog.navigate({"docs/README.md", {}}));
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
                                     Q_ARG(QUrl, QUrl("methods/"))));
    QCOMPARE(dialog.currentTopic().path, QString("docs/methods/README.md"));
    QVERIFY(dialog.navigate(userGuideTopic()));
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
        Q_ARG(QUrl, QUrl("https://github.com/ChenCLian/AstroBatch/tree/main/docs/methods"))));
    QCOMPARE(dialog.currentTopic().path, QString("docs/methods/README.md"));
    QCOMPARE(capturedHelpUrls.size(), 1);
    QVERIFY(!dialog.navigate({"../licenses/", {}}));
    QCOMPARE(capturedHelpUrls.size(), 1);
}

void UiTests::parameterHelpTracksSelectedNode()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("sine_source").nodeId;
    const auto scale = doc.addChild(root, ChildSlot::Down, "affine").nodeId;
    const auto mtm = doc.addChild(scale, ChildSlot::Down, "mtm").nodeId;
    ParameterEditor editor; editor.resize(340, 760);
    editor.showNode(doc.node(scale), doc.model().registry()); editor.show();
    const auto original = editor.parameters();
    auto *row = editor.findChild<QWidget *>("parameterRow_factor"); QVERIFY(row);
    auto *button = row->findChild<QToolButton *>("ParameterHelp"); QVERIFY(button);
    QVERIFY(button->toolTip().isEmpty());
    QVERIFY(!button->accessibleDescription().isEmpty());
    QTest::mouseClick(button, Qt::LeftButton);
    auto *dialog = editor.findChild<HelpDialog *>("helpDialog"); QVERIFY(dialog);
    QCOMPARE(dialog->currentTopic().anchor, QString("m-affine-p-factor"));
    QCOMPARE(editor.parameters(), original); QVERIFY(!editor.dirty());
    editor.showNode(doc.node(mtm), doc.model().registry());
    auto *label = editor.findChild<QLabel *>("parameterLabel_smooth_window"); QVERIFY(label);
    QVERIFY(label->text().contains(QString::fromUtf8("比例")));
    QCOMPARE(editor.parameters().value("smooth_window").toDouble(), .2);
    editor.findChild<QToolButton *>("methodHelp")->click();
    QCOMPARE(dialog->currentTopic().anchor, QString("m-mtm"));
    row = editor.findChild<QWidget *>("parameterRow_smooth_window"); QVERIFY(row);
    button = row->findChild<QToolButton *>("ParameterHelp");
    button->setFocus();
    QTest::keyClick(button, Qt::Key_Space);
    QCOMPARE(dialog->currentTopic().anchor, QString("m-mtm-p-smooth_window"));
    QVERIFY(!editor.dirty());
    dialog->close();
    SettingsDialog settings(AppSettings{}); settings.show();
    settings.findChild<QPushButton *>("openUserGuide")->click();
    QVERIFY(settings.findChild<HelpDialog *>("helpDialog"));
    settings.close();
    QTemporaryDir folder;
    AppSettings appSettings; appSettings.databasePath = folder.filePath("help.sqlite");
    MainWindow main(appSettings, false); main.show(); main.activateWindow(); main.setFocus();
    QTest::qWait(30);
    QTest::keyClick(&main, Qt::Key_F1);
    QTRY_VERIFY(main.findChild<HelpDialog *>("helpDialog"));
    main.close();
}

void UiTests::licenseNoticesOffline()
{
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct ResetHandler { ~ResetHandler() { QDesktopServices::unsetUrlHandler("https"); } } reset;
    for (const auto &theme : {QString("light"), QString("dark")})
        for (const auto &language : {QString("zh"), QString("en")})
        {
            setLanguage(language);
            ThemeManager::instance().apply(*qApp, theme);
            SettingsDialog settings(AppSettings{}); settings.show();
            QTest::qWait(10);
            QVERIFY(settings.width() < 560);
            auto *button = settings.findChild<QPushButton *>("settingsLicense");
            QVERIFY(button); QVERIFY(!button->autoDefault());
            QVERIFY(settings.rect().contains(QRect(button->mapTo(&settings, QPoint()), button->size())));
            button->click();
            auto *dialog = settings.findChild<HelpDialog *>("helpDialog"); QVERIFY(dialog);
            QCOMPARE(dialog->currentTopic().path, licenseTopic().path);
            QCOMPARE(dialog->windowTitle(), text("ui.license"));
            auto *browser = dialog->findChild<QTextBrowser *>("helpBrowser"); QVERIFY(browser);
            QVERIFY(browser->toPlainText().contains("Copyright (C) 2026 ZiJie Chen"));
            QVERIFY(browser->toPlainText().contains("GPL-3.0-only"));
            QVERIFY(dialog->navigate({"LICENSE", {}}));
            QFile license(":/help/LICENSE"); QVERIFY(license.open(QIODevice::ReadOnly));
            QString expected = QString::fromUtf8(license.readAll()); expected.replace("\r\n", "\n");
            QCOMPARE(browser->toPlainText().trimmed(), expected.trimmed());
            QVERIFY(dialog->navigate({"licenses/AstroGeoFit_LICENSE.txt", {}}));
            QFile astroLicense(":/help/licenses/AstroGeoFit_LICENSE.txt");
            QVERIFY(astroLicense.open(QIODevice::ReadOnly));
            expected = QString::fromUtf8(astroLicense.readAll()); expected.replace("\r\n", "\n");
            QCOMPARE(browser->toPlainText().trimmed(), expected.trimmed());
            QVERIFY(dialog->navigate({"SOURCE_CODE.md", {}}));
            QVERIFY(browser->toPlainText().contains("Corresponding Source"));
            QVERIFY(!localHelpAvailable({"LICENSE", "nonexistent"}));
            QVERIFY(dialog->navigate(userGuideTopic()));
            QCOMPARE(dialog->windowTitle(), text("help.title"));
            settings.close();
        }
    QVERIFY(capturedHelpUrls.isEmpty());
    setLanguage("zh");
    ThemeManager::instance().apply(*qApp, "light");
}

void UiTests::plotDestinationSettings()
{
    QTemporaryDir dir;
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        QVERIFY(ThemeManager::instance().apply(*qApp, "light").isEmpty());
        AppSettings values; values.databasePath = dir.filePath("settings-" + language + ".sqlite");
        values.language = language;
        SettingsDialog dialog(values); dialog.show();
        auto *choice = dialog.findChild<QComboBox *>("settingsDefaultPlotTarget"); QVERIFY(choice);
        QCOMPARE(choice->currentData().toBool(), true);
        QVERIFY(dialog.width() < 560);
        QVERIFY(dialog.rect().contains(QRect(choice->mapTo(&dialog, QPoint()), choice->size())));
        const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
        if (!capture.isEmpty()) QVERIFY(dialog.grab().save(capture + "/settings_" + language + ".png"));
        choice->setCurrentIndex(choice->findData(false));
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QTRY_COMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!dialog.values().defaultPlotNewPage);
        auto config = dir.filePath("settings-" + language + ".ini");
        QVERIFY(dialog.values().save(config).isEmpty());
        SettingsDialog reopened(AppSettings::load(config));
        QCOMPARE(reopened.findChild<QComboBox *>("settingsDefaultPlotTarget")->currentData().toBool(), false);
    }
    setLanguage("zh");
}
void UiTests::settingsLinksAndKeyboardNavigation()
{
    setLanguage("zh");
    capturedHelpUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureHelpUrl");
    struct ResetHandler { ~ResetHandler() { QDesktopServices::unsetUrlHandler("https"); } } reset;
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        AppSettings values; values.language = language;
        values.databasePath = QString(180, QChar('a')) + "/results.sqlite";
        SettingsDialog dialog(values); dialog.show(); dialog.activateWindow();
        QTest::qWait(20);
        QVERIFY(dialog.width() < 560);
        auto *path = dialog.findChild<QLineEdit *>("settingsDatabase");
        QCOMPARE(path->toolTip(), values.databasePath);
        auto *manual = dialog.findChild<QPushButton *>("openUserGuide");
        QVERIFY(manual->text().contains(language == "zh" ? QString::fromUtf8("（F1）") : QString("(F1)")));
        dialog.findChild<QPushButton *>("settingsContact")->click();
        auto *authorHelp=dialog.findChild<HelpDialog *>("helpDialog");
        QVERIFY(authorHelp);QCOMPARE(authorHelp->currentTopic().path,QString("AUTHORS.md"));
        QVERIFY(capturedHelpUrls.isEmpty());
        QTest::keyClick(&dialog, Qt::Key_F1);
        QTRY_VERIFY(dialog.findChild<HelpDialog *>("helpDialog"));
        dialog.findChild<HelpDialog *>("helpDialog")->close();
        dialog.activateWindow(); QTest::qWait(20);
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QTRY_VERIFY(!dialog.isVisible());
    }
    setLanguage("zh");
    QTemporaryDir folder;
    AppSettings settings; settings.databasePath = folder.filePath("keys.sqlite");
    MainWindow window(settings, false); window.resize(1500, 900); window.show(); window.activateWindow();
    QTest::qWait(50);
    const auto page = [&] { return window.results()->isVisible() ? 1 : 0; };
    QCOMPARE(page(), 0);
    QTest::keyClick(&window, Qt::Key_2, Qt::ControlModifier);
    QTRY_COMPARE(page(), 1);
    auto *search = window.findChild<QLineEdit *>("recordSearch");
    QVERIFY(search && search->isVisible()); search->setFocus();
    QTest::keyClicks(search, "adAD");
    QCOMPARE(search->text(), QString("adAD")); QCOMPARE(page(), 1);
    QTest::keyClick(search, Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(page(), 0);
    for (const auto &entry : {qMakePair(QString("previewSidePanel"), Qt::Key_1),
                             qMakePair(QString("parameterSidePanel"), Qt::Key_2),
                             qMakePair(QString("catalogSidePanel"), Qt::Key_3)})
    {
        auto *panel = window.findChild<CollapsiblePanel *>(entry.first); QVERIFY(panel);
        const bool initial = panel->isCollapsed();
        const auto *grip = panel->findChild<QAbstractButton *>("sidePanelGrip"); QVERIFY(grip);
        const QKeySequence expectedShortcut(
            QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, entry.second));
        QCOMPARE(grip->shortcut(), expectedShortcut);
        QVERIFY(grip->toolTip().contains(expectedShortcut.toString(QKeySequence::NativeText)));
        QTest::keyClick(&window, entry.second, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(panel->isCollapsed(), !initial);
        QTest::keyClick(&window, entry.second, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(panel->isCollapsed(), initial);
        window.showResults();
        QTest::keyClick(&window, entry.second, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::qWait(20);
        QCOMPARE(panel->isCollapsed(), initial); // Hidden task panels must not react.
        window.setPage(0);
    }
    bool opened = false;
    QTimer poll;
    connect(&poll, &QTimer::timeout, &window, [&] {
        if (auto *dialog = window.findChild<QDialog *>("settingsDialog"); dialog && dialog->isVisible())
        {
            opened = true;
            QTest::keyClick(dialog, Qt::Key_2, Qt::ControlModifier);
            const bool stayedOnTask = page() == 0;
            dialog->reject();
            QVERIFY(stayedOnTask); // Window shortcuts do not leak through modal settings.
        }
    });
    poll.start(10);
    QTest::keyClick(&window, Qt::Key_Comma, Qt::ControlModifier);
    QTRY_VERIFY(opened);
    poll.stop();
    window.activateWindow(); QTest::qWait(20);
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!window.findChild<QDialog *>("settingsDialog"));
    QVERIFY(window.isVisible());
    window.close();
}

void UiTests::initTestCase()
{
    setLanguage("zh");
    applyTheme(*qApp);
    qRegisterMetaType<NodeId>("aster::NodeId");
    qRegisterMetaType<ChildSlot>("aster::ChildSlot");
}
void UiTests::canvasFractionalMtmAndExtremeZoom()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("sine_source").nodeId;
    const auto mtm = doc.addChild(root, ChildSlot::Down, "mtm", {{"nw", 2.5}});
    QVERIFY(mtm.ok());
    WorkflowCanvas view(&doc);
    view.resize(900, 600);
    bool correctLabel = false;
    for (auto *item : view.scene()->items())
        if (item->data(1).toUuid() == mtm.nodeId)
            correctLabel = item->toolTip().contains("NW 2.5");
    QVERIFY(correctLabel);
    const auto second = doc.addRoot("sine_source").nodeId;
    auto definition = doc.model().definition();
    definition.layout[second] = {1000000, 1000000};
    QVERIFY(doc.replaceDefinition(definition).ok());
    view.rebuild();
    view.fitWorkflow();
    // Distant legacy coordinates no longer waste screen space.
    QVERIFY(view.transform().m11() >= .25);
    view.scale(.001, .001); // Still exercise the extreme zoom rendering guard.
    QVERIFY(view.transform().m11() < .25);
    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QElapsedTimer timer;
    timer.start();
    view.render(&painter);
    QVERIFY2(timer.elapsed() < 3000, "Distant roots must not cause unbounded grid rendering");
    const auto before = view.transform().m11();
    const QPointF at = view.viewport()->rect().center();
    QWheelEvent wheel(at, view.viewport()->mapToGlobal(at.toPoint()), {}, {0, 120},
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &wheel);
    QVERIFY(view.transform().m11() > before);
}
void UiTests::draftBindingsSurviveUndo()
{
    WorkflowDocument doc(makeScientificRegistry());
    auto root = doc.addRoot("file_source").nodeId;
    doc.setFiles(root, {"/tmp/test-a.csv"});
    auto child = doc.addChild(root, ChildSlot::Down, "preprocess").nodeId;
    QVERIFY(doc.removeSubtree(root).ok());
    QVERIFY(doc.model().definition().nodes.isEmpty());
    QVERIFY(doc.undo().ok());
    QVERIFY(doc.node(child));
    QCOMPARE(doc.files(root).size(), 1);
    QVERIFY(doc.plan().validation.ok());
    QCOMPARE(doc.plan().requests.size(), 1);
    QVERIFY(doc.redo().ok());
    QVERIFY(doc.model().definition().nodes.isEmpty());
}
void UiTests::sourceBindingsAreSeparate()
{
    WorkflowDocument doc(makeScientificRegistry());
    SourcePanel panel(&doc);
    auto a = doc.addRoot("file_source").nodeId;
    auto b = doc.addRoot("file_source").nodeId;
    doc.setFiles(a, {"/tmp/a.csv", "/tmp/a.csv"});
    doc.setFiles(b, {"/tmp/b.csv"});
    panel.refresh();
    panel.selectRoot(b);
    panel.addFiles({"/tmp/c.csv"});
    QCOMPARE(doc.files(a).size(), 1);
    QCOMPARE(doc.files(b).size(), 2);
    QVERIFY(doc.removeSubtree(a).ok());
    auto plan = doc.plan();
    QVERIFY(plan.validation.ok());
    QCOMPARE(plan.requests.size(), 2);
    QCOMPARE(plan.requests[0].root, b);
}
void UiTests::hierarchicalTaskMenu()
{
    TaskCatalog catalog(makeScientificRegistry());
    QString selected;
    auto *menu = catalog.menu(
        &catalog, [](const TaskDescriptor &d) { return !d.inputKind; },
        [&](QString key) { selected = key; });
    QVERIFY(menu->actions().size() > 0);
    QVERIFY(menu->actions().front()->menu());
    QVERIFY(findAction(menu, "file_source"));
    QVERIFY(findAction(menu, "sine_source"));
    QVERIFY(!findAction(menu, "mtm"));
    QVERIFY(!findAction(menu, "input_series"));
    findAction(menu, "sine_source")->trigger();
    QCOMPARE(selected, QString("sine_source"));
    delete menu;
}
void UiTests::astronomySourcesUseSharedUi()
{
    for (const auto& language : {"zh", "en"}) {
        setLanguage(language);
        WorkflowDocument doc(makeScientificRegistry());
        TaskCatalog catalog(makeScientificRegistry());
        QString chosen;
        auto* menu = catalog.menu(&catalog, [](const TaskDescriptor& d) { return !d.inputKind; },
                                  [&](QString key) { chosen = key; });
        for (const auto& key : {"orbital_source", "etp_source", "insolation_source"}) {
            auto* action = findAction(menu, key); QVERIFY(action); action->trigger(); QCOMPARE(chosen, QString(key));
            const auto root = doc.addRoot(key).nodeId; QVERIFY(!root.isNull());
            ParameterEditor editor; editor.showNode(doc.node(root), doc.model().registry());
            auto* end = editor.findChild<QDoubleSpinBox*>("parameter_age_end_ka"); QVERIFY(end);
            end->setValue(100); QVERIFY(editor.dirty());
            QVERIFY(doc.setParameters(root, editor.parameters()).ok());
            QCOMPARE(doc.node(root)->parameters["age_end_ka"].toDouble(), 100.0);
            const auto& descriptor = doc.model().registry().find(key)->descriptor;
            for (const auto& p : descriptor.parameters) QVERIFY(!parameterLabel(p).startsWith("parameter."));
        }
        const auto plan = doc.plan(); QVERIFY(plan.validation.ok()); QCOMPARE(plan.requests.size(), 3);
        for (const auto& request : plan.requests) {
            QVERIFY(!request.source.data);
            QCOMPARE(WorkflowExecutor().run(request).state, RunState::Succeeded);
        }
        delete menu;
    }
    setLanguage("en");
}
void UiTests::parameterValuesAreIndependent()
{
    WorkflowDocument doc(makeScientificRegistry());
    auto root = doc.addRoot("sine_source").nodeId;
    auto a = doc.addChild(root, ChildSlot::Left, "mtm", {{"nw", 2}}).nodeId;
    auto b = doc.addChild(root, ChildSlot::Right, "mtm", {{"nw", 4}}).nodeId;
    ParameterEditor editor;
    editor.resize(280, 360);
    editor.showNode(doc.node(a), doc.model().registry());
    auto *spin = editor.findChild<QDoubleSpinBox *>("parameter_nw");
    QVERIFY(spin);
    QCOMPARE(spin->value(), 2);
    spin->setValue(5.5);
    QVERIFY(editor.dirty());
    QVERIFY(doc.setParameters(a, editor.parameters()).ok());
    editor.showNode(doc.node(b), doc.model().registry());
    QCOMPARE(editor.findChild<QDoubleSpinBox *>("parameter_nw")->value(), 4);
    QCOMPARE(doc.node(a)->parameters["nw"].toDouble(), 5.5);
}
void UiTests::canvasClickAndAtomicDelete()
{
    WorkflowDocument doc(makeScientificRegistry());
    auto root = doc.addRoot("sine_source").nodeId;
    auto child = doc.addChild(root, ChildSlot::Down, "preprocess").nodeId;
    auto leaf = doc.addChild(child, ChildSlot::Right, "mtm").nodeId;
    WorkflowCanvas canvas(&doc);
    canvas.resize(900, 600);
    canvas.show();
    canvas.setSelectedNode(root);
    canvas.fitWorkflow();
    QSignalSpy selected(&canvas, &WorkflowCanvas::nodeSelected);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier,
                      canvas.mapFromScene(canvas.nodeRect(child).center()));
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.front()[0].toUuid(), child);
    connect(&doc, &WorkflowDocument::changed, &canvas, &WorkflowCanvas::rebuild);
    QVERIFY(doc.removeSubtree(child).ok());
    QVERIFY(!doc.node(leaf));
    QVERIFY(!canvas.nodeRect(root).isEmpty());
    QVERIFY(canvas.nodeRect(child).isEmpty());
    QVERIFY(doc.undo().ok());
    QVERIFY(doc.node(leaf));
}
void UiTests::canvasRejectsIllegalDrop()
{
    WorkflowDocument doc(makeScientificRegistry());
    auto root = doc.addRoot("sine_source").nodeId;
    WorkflowCanvas canvas(&doc);
    canvas.resize(900, 650);
    canvas.show();
    canvas.setSelectedNode(root);
    canvas.fitWorkflow();
    QSignalSpy dropped(&canvas, &WorkflowCanvas::methodDropped);
    QMimeData mime;
    mime.setData(TaskMimeType, "affine");
    auto pos = canvas.addViewportPosition(root);
    QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &drop);
    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.front()[1].toUuid(), root);
    // Dragging a source into a child slot must never create an implicit parent.
    mime.setData(TaskMimeType, "file_source");
    QDragEnterEvent again(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &again);
    QDropEvent invalid(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &invalid);
    QVERIFY(!invalid.isAccepted());
    QCOMPARE(dropped.count(), 1);
}
void UiTests::backgroundEventsAndCancellation()
{
    auto registry = makeScientificRegistry();
    TaskDescriptor d;
    d.key = "slow_source";
    d.label = "Slow";
    d.category = {"Test"};
    d.outputs = {{"series", "Series", DataKind::Series, true}};
    d.defaultOutput = "series";
    QVERIFY(
        registry.add(d, [](const QJsonObject &) { return std::make_unique<SlowSource>(); }).ok());
    WorkflowModel model(registry);
    model.addRoot("slow_source", {0, 0});
    auto plan = prepareRuns(model.freeze().snapshot, {});
    QTemporaryDir dir;
    RunController runner;
    bool onGui = true, runningSeen = false;
    connect(&runner, &RunController::eventReady, this,
            [&](const RunEvent &e)
            {
                onGui = onGui && QThread::currentThread() == qApp->thread();
                runningSeen |= e.state == NodeState::Running;
            });
    QSignalSpy finished(&runner, &RunController::batchReady);
    QVERIFY(runner.start(plan, dir.filePath("cancel.sqlite"), 2));
    QVERIFY(!runner.start(plan, dir.filePath("other.sqlite"), 2));
    QTRY_VERIFY_WITH_TIMEOUT(runningSeen, 5000);
    runner.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!runner.busy(), 5000);
    QVERIFY(onGui);
    QCOMPARE(finished.count(), 1);
    auto batch = qvariant_cast<std::shared_ptr<const BatchResult>>(finished[0][0]);
    QCOMPARE(batch->items[0].computation, RunState::Cancelled);
    QVERIFY(batch->items[0].persistence.saved());
}
void UiTests::latestResultRequestWins()
{
    QTemporaryDir dir;
    auto path = dir.filePath("results.sqlite");
    qint64 a, b;
    {
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        a = repo.save(storedRun("first")).recordId;
        b = repo.save(storedRun("latest")).recordId;
    }
    ResultLoader loader;
    QVector<qint64> received;
    connect(&loader, &ResultLoader::ready, this,
            [&](qint64 id, std::shared_ptr<const LoadResult> result)
            {
                if (result->ok())
                    received.push_back(id);
            });
    loader.request(path, a);
    loader.request(path, b);
    QTRY_COMPARE_WITH_TIMEOUT(received.size(), 1, 5000);
    QCOMPARE(received.front(), b);
}
void UiTests::resultLoaderDoesNotRecreateMissingDatabase()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("removed.sqlite");
    ResultLoader loader;
    QSignalSpy done(&loader, &ResultLoader::ready);
    loader.request(path, 1);
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
    const auto result = qvariant_cast<std::shared_ptr<const LoadResult>>(done[0][1]);
    QVERIFY(result);
    QVERIFY(!result->ok());
    QVERIFY(!QFileInfo::exists(path));
}
void UiTests::typedPlotAndRepeatedClear()
{
    PlotView view;
    view.resize(900, 500);
    view.show();
    auto spectra = spectrumResult();
    auto grid = gridResult();
    for (int i = 0; i < 12; ++i)
    {
        view.showOutput(spectra, "spectrum");
        QCOMPARE(view.plottedCount(), 3); // spectrum, SWA and visible peak markers
        view.setLogY(true);
        view.fit();
        view.showOutput(grid, "power");
        QCOMPARE(view.plottedCount(), 2);
        view.fit();
        view.clear();
        QCOMPARE(view.plottedCount(), 0);
        QVERIFY(!view.hasOutput());
    }
    view.showOutput(spectra, "peaks");
    QVERIFY(dataAs<TableData>(view.currentData()));
    QCOMPARE(view.plottedCount(), 0);
}
void UiTests::contextMenuAfterClear()
{
    auto view = std::make_unique<PlotView>();
    view->resize(800, 500);
    view->show();
    view->showOutput(spectrumResult(), "spectrum");
    auto *plot = view->findChild<QCustomPlot *>("resultPlot");
    QVERIFY(plot);
    view->clear();
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(90, 90),
                            plot->mapToGlobal(QPoint(90, 90)));
    QApplication::sendEvent(plot, &event);
    QCoreApplication::processEvents();
    auto menus = view->findChildren<QMenu *>();
    QVERIFY(!menus.isEmpty());
    menus.front()->actions().last()->trigger();
    view.reset();
    QCoreApplication::processEvents();
}
void UiTests::exportNamesUseRecordMetadata()
{
    QTemporaryDir dir;
    const auto db = dir.filePath("names.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(db).isEmpty());
    const auto save = [&](RunResult run) {
        return OutputReference{db, repo.save(run).recordId, run.runId, run.root, "series", "Series"};
    };
    auto original = storedRun("old source");
    original.provenance["path"] = R"(C:\data\冰芯.A.csv)";
    const auto a = save(original);
    const auto b = save(storedRun("Signal 0.5 ka"));
    const auto c = save(storedRun("A / B"));
    QVERIFY(a.valid() && b.valid() && c.valid());
    QCOMPARE(suggestedExportFileName({a}, "1_series", "csv"), QString("冰芯.A_1_series.csv"));
    QCOMPARE(suggestedExportFileName({b}, {}, "png"), QString("Signal 0.5 ka.png"));
    QCOMPARE(suggestedExportFileName({a, a, b, c}, {}, "pdf"),
             QString("冰芯.A + Signal 0.5 ka (+1).pdf"));
    QCOMPARE(suggestedExportFileName({c}, {}, "pdf"), QString("A _ B.pdf"));
    auto reused = storedRun(QString("ID %1 · series").arg(a.recordId));
    reused.provenance["database_source"] = a.toJson();
    const auto derived = save(reused);
    QVERIFY(derived.valid());
    QCOMPARE(suggestedExportFileName({derived}, {}, "png"), QString("冰芯.A.png"));
    QVERIFY(repo.updateMetadata(a.recordId, "CON", {}).isEmpty());
    QCOMPARE(suggestedExportFileName({derived}, {}, "png"), QString("_CON.png"));
    QVERIFY(repo.updateMetadata(a.recordId, QString("冰芯🌍").repeated(120), {}).isEmpty());
    const auto longName = suggestedExportFileName({a, b, c}, "1_series", "csv");
    QVERIFY(longName.toUtf8().size() <= 255);
    QVERIFY(!longName.contains(QChar::ReplacementCharacter));
    QVERIFY(longName.endsWith(" + Signal 0.5 ka (+1)_1_series.csv"));
    auto stale = a;
    stale.runId = QUuid::createUuid();
    QCOMPARE(suggestedExportFileName({stale}, {}, "png"), QString("AstroBatch.png"));
}

void UiTests::exportDialogsFollowTheirOwnTargets()
{
    QTemporaryDir dir;
    const auto db = dir.filePath("dialogs.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(db).isEmpty());
    auto run = storedRun("file");
    run.provenance["path"] = "/samples/80001.csv";
    const auto a = repo.save(run).recordId;
    const auto b = repo.save(storedRun("Site B")).recordId;
    QVERIFY(a > 0 && b > 0);
    const bool oldSetting = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    const auto restore = qScopeGuard([oldSetting] {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, oldSetting);
    });
    ResultsPage page(makeScientificRegistry());
    page.setDatabase(db);
    page.show();
    auto *workspace = page.workspace();
    page.openRecord(a);
    QTRY_COMPARE(page.recordId(), a);
    QTRY_VERIFY(!workspace->loading() && page.plotView() && page.plotView()->hasPlot());
    const auto first = workspace->currentReference();
    page.openRecord(b);
    QTRY_COMPARE(page.recordId(), b);
    QTRY_VERIFY(!workspace->loading() && page.plotView() && page.plotView()->hasPlot());
    const auto second = workspace->currentReference();
    workspace->openDefault(first);
    QTRY_COMPARE(workspace->currentReference().recordId, a);
    QTRY_VERIFY(!workspace->loading() && page.plotView()->hasPlot());
    page.selectStep(0, false);
    QCOMPARE(page.recordId(), b);

    auto *csv = page.findChild<QPushButton *>("exportCsv");
    auto *figure = page.findChild<QPushButton *>("exportPng");
    QVERIFY(csv && csv->isEnabled() && figure && figure->isEnabled());
    bool sawOverwrite = false;
    const auto choose = [&](QPushButton *button, const QString &format, bool whole,
                            const QString &manualPath = {}) {
        QString suggestion;
        bool submitted = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &page, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *options = dynamic_cast<FigureExportDialog *>(modal)) {
                for (auto *box : options->findChildren<QComboBox *>()) {
                    const int i = box->findText(format);
                    if (i >= 0) box->setCurrentIndex(i);
                    else if (whole) box->setCurrentIndex(box->findText(text("ui.whole_current_page")));
                }
                options->accept();
            } else if (auto *dialog = qobject_cast<QFileDialog *>(modal)) {
                if (!submitted) suggestion = QFileInfo(dialog->selectedFiles().value(0)).fileName();
                if (manualPath.isEmpty() || submitted) dialog->reject();
                else if (auto *edit = dialog->findChild<QLineEdit *>("fileNameEdit")) {
                    edit->setText(manualPath);
                    submitted = true;
                    QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
                }
            } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                sawOverwrite = box->standardButtons().testFlag(QMessageBox::No);
                box->reject();
            } else if (auto *other = qobject_cast<QDialog *>(modal)) other->reject();
        });
        timer.start(20);
        button->click();
        return suggestion;
    };
    QSignalSpy finished(page.exporter(), &ExportController::finished);
    const auto manual = dir.filePath("chosen");
    QCOMPARE(choose(csv, {}, false, manual), QString("Site B_1_series.csv"));
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(finished.front()[0].toString(), manual + ".csv");
    QVERIFY2(finished.front()[1].toString().isEmpty(), qPrintable(finished.front()[1].toString()));
    QVERIFY(QFileInfo::exists(manual + ".csv"));
    QCOMPARE(choose(csv, {}, false, manual), QString("Site B_1_series.csv"));
    QVERIFY(sawOverwrite);
    QCOMPARE(finished.size(), 1);
    QSignalSpy imageSaved(page.figureExporter(), &FigureExportController::finished);
    const auto imagePath = dir.filePath("自选图名");
    QCOMPARE(choose(figure, "PNG", false, imagePath), QString("80001.png"));
    QTRY_COMPARE(imageSaved.size(), 1);
    QVERIFY2(imageSaved.front()[1].toString().isEmpty(), qPrintable(imageSaved.front()[1].toString()));
    QVERIFY(!QImage(imagePath + ".png").isNull());
    QVERIFY(repo.updateMetadata(a, "Site A", {}).isEmpty());
    QCOMPARE(choose(figure, "PDF", false), QString("Site A.pdf"));
    workspace->open(second, PlotWorkspace::Target::Subplot);
    QTRY_VERIFY(!workspace->loading() && workspace->figureCells(true).size() == 2);
    QCOMPARE(choose(figure, "PNG", true), QString("Site A + Site B.png"));
    QCOMPARE(choose(figure, "PDF", false), QString("Site B.pdf"));
}

void UiTests::exportPreservesRawValues()
{
    QTemporaryDir dir;
    PlotView view;
    view.showOutput(gridResult(), "power");
    auto path = dir.filePath("grid.csv");
    QVERIFY(view.exportCsv(path).isEmpty());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto bytes = file.readAll();
    QVERIFY(bytes.contains("\n2,1,2,3,4\n"));
    QVERIFY(bytes.contains("\n8,3,4,5,6\n"));
    QCOMPARE(dataAs<GridData>(view.currentData())->y, QVector<double>({2, 4, 8}));
}
void UiTests::gridCoordinatesAndInvalidInput()
{
    PlotView view;
    view.showOutput(gridResult(), "power");
    auto *plot = view.findChild<QCustomPlot *>("resultPlot");
    QVERIFY(plot);
    view.fit();
    QCOMPARE(plot->yAxis->range().lower, 1.);
    QCOMPARE(plot->yAxis->range().upper, 3.);
    auto grid = *dataAs<GridData>(gridResult().outputs["power"]);
    grid.y = {2, 3, 4};
    view.showOutput(NodeResult::success({{"power", makeData(grid)}}), "power");
    view.fit();
    QCOMPARE(plot->yAxis->range().lower, 2.);
    QCOMPARE(plot->yAxis->range().upper, 4.);
    view.showOutput(NodeResult::success({{"power", makeData(GridData{})}}), "power");
    QVERIFY(!view.hasOutput());
    QCOMPARE(view.plottedCount(), 0);
}
void UiTests::fullWindowRunUsesEditedParameters()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("window.sqlite");
    MainWindow window(settings, false);
    window.resize(1366, 768);
    window.show();
    auto fixture = aster::testsupport::installWorkspaceFixture(window);
    QVERIFY(fixture);
    QCOMPARE(window.document()->model().definition().nodes.size(), 6);
    auto *spin = window.findChild<QDoubleSpinBox *>("parameter_nw");
    QVERIFY(spin);
    const auto editedItem=window.findChild<SourcePanel *>()->currentSource().provenance["dataset_id"].toString();
    QVERIFY(!editedItem.isEmpty());
    spin->setValue(4);
    QSignalSpy loaded(window.results(), &ResultsPage::recordLoaded);
    QTRY_VERIFY(window.findChild<SourcePanel *>()->readyToRun());
    window.startRun();
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 10000);
    // The selected record and the directory are loaded by separate workers;
    // recordLoaded does not imply that the directory query has completed.
    QTRY_COMPARE_WITH_TIMEOUT(window.results()->recordCount(), 3, 10000);
    QVERIFY(window.results()->recordId() > 0);
    window.results()->selectStep(3, true);
    QTRY_VERIFY(window.results()->plotView()->plottedCount() > 0);
    ResultRepository repo;
    QVERIFY(repo.open(settings.databasePath).isEmpty());
    auto run = repo.load(window.results()->recordId());
    QVERIFY(run.ok());
    int editedRecords=0;
    for (const auto &record : repo.list().records) {
        const auto saved=repo.load(record.id); QVERIFY(saved.ok());
        const bool edited=saved.run.provenance["dataset_id"].toString()==editedItem;
        if (edited) ++editedRecords;
        const auto def=workflowFromJson(saved.run.workflowSnapshot).definition;
        for (const auto &n : def.nodes) if (n.method=="mtm") {
            QCOMPARE(n.parameters["nw"].toInt(),edited ? 4 : 3);
            QVERIFY(n.parameterOverrides.isEmpty()); // Saved run contains its actual settings.
        }
    }
    QCOMPARE(editedRecords,1);
    auto *steps = window.findChild<QTableWidget *>("resultSteps");
    QCOMPARE(steps->rowCount(), 6);
    window.results()->selectStep(4, true);
    QTRY_VERIFY(dataAs<GridData>(window.results()->plotView()->currentData()));
    window.results()->selectStep(5, true);
    QTRY_VERIFY(dataAs<TableData>(window.results()->plotView()->currentData()));
}
void UiTests::smallDesktopLayout()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("layout.sqlite");
    MainWindow window(settings, false);
    window.resize(1200, 720);
    window.show();
    auto fixture = aster::testsupport::installWorkspaceFixture(window);
    QVERIFY(fixture);
    QCoreApplication::processEvents();
    for (auto *area : window.findChildren<QScrollArea *>())
        if (area->isVisible())
            QCOMPARE(area->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    for (auto *control : window.findChildren<QComboBox *>())
        if (control->isVisible())
        {
            auto pos = control->mapTo(&window, QPoint(0, 0));
            QVERIFY2(pos.x() >= 0 && pos.x() + control->width() <= window.width(),
                     qPrintable(control->objectName()));
        }
    auto *editor = window.findChild<ParameterEditor *>();
    QVERIFY(editor);
    QVERIFY(window.findChild<CollapsiblePanel *>("parameterSidePanel")->isCollapsed() || editor->width() >= 200);
    QVERIFY(window.canvas()->viewport()->width() >= 280);
}
void UiTests::closeDuringRunWaitsForWorker()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("close.sqlite");
    MainWindow window(settings, false);
    window.show();
    auto fixture = aster::testsupport::installWorkspaceFixture(window);
    QVERIFY(fixture);
    // closeEvent asks to cancel. A queued answer exercises the actual modal path.
    QTRY_VERIFY(window.findChild<SourcePanel *>()->readyToRun());
    window.startRun();
    QVERIFY(window.runner()->busy());
    QTimer::singleShot(0,
                       []
                       {
                           if (auto *box =
                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                               QTest::mouseClick(box->button(QMessageBox::Yes), Qt::LeftButton);
                       });
    window.close();
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 10000);
}

void UiTests::fileOptionsRemainPerInput()
{
    QTemporaryDir dir;
    const auto a = dir.filePath("a.csv"), b = dir.filePath("b.txt");
    {
        QFile f(a);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("time,A,B\n0,10,20\n1,11,21\n");
    }
    {
        QFile f(b);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("0 30\n1 31\n");
    }
    WorkflowDocument document(makeScientificRegistry());
    const auto root = document.addRoot("file_source").nodeId;
    SourcePanel panel(&document);
    panel.addFiles({a, b});
    QTRY_VERIFY(panel.findChild<FileOptionsEditor *>()->ready());
    panel.findChild<QLineEdit *>("fileXUnit")->setText("ka");
    panel.applyCurrentOptions();
    QCOMPARE(document.fileOptions(root, a)["x_unit"].toString(), QString("ka"));
    auto options = document.fileOptions(root, a);
    options["y_column"] = 2;
    options["x_unit"] = "ka";
    QVERIFY(document.setFileOptions(root, {a}, options));
    QCOMPARE(document.fileOptions(root, b)["y_column"].toInt(), 1);
    const auto frozen = document.plan();
    QVERIFY(frozen.validation.ok());
    options["y_column"] = 1;
    QVERIFY(document.setFileOptions(root, {a}, options));
    const auto run = WorkflowExecutor().run(frozen.requests[0]);
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(dataAs<SeriesData>(run.nodes[run.root].outputs["series"])->y,
             QVector<double>({20, 21}));
    QCOMPARE(
        dataAs<SeriesData>(WorkflowExecutor().run(frozen.requests[1]).nodes[root].outputs["series"])
            ->y,
        QVector<double>({30, 31}));
    document.setFiles(root, {a, b, a});
    QCOMPARE(document.files(root).size(), 2);
    QCOMPARE(document.fileOptions(root, a)["x_unit"].toString(), QString("ka"));
    FileOptionsEditor editor;
    editor.setFile(a, options);
    auto *x = editor.findChild<QComboBox *>("fileXColumn");
    auto *y = editor.findChild<QComboBox *>("fileYColumn");
    QTRY_COMPARE(x->count(), 3);
    QCOMPARE(x->itemText(0), QString("1 · time"));
    y->setCurrentIndex(2);
    QCOMPARE(editor.options()["y_column"].toInt(), 2);
    editor.setFile(a, options);
    editor.setFile(b, document.fileOptions(root, b));
    QTRY_COMPARE(x->count(), 2);
    QVERIFY(
        editor.findChild<QLabel *>("fileProbeInfo")->text().contains(QString::fromUtf8("无表头")));
}
void UiTests::directoryServiceReentrantEnqueue()
{
    QFETCH(bool,onMutation);
    QTemporaryDir dir; const auto path=dir.filePath("directory.sqlite");
    qint64 id;
    { ResultRepository repo; QVERIFY(repo.open(path).isEmpty()); id=repo.save(storedRun("original")).recordId; }
    RecordDirectoryService service;
    QSignalSpy busy(&service,&RecordDirectoryService::busyChanged);
    QSignalSpy edited(&service,&RecordDirectoryService::edited);
    bool queued=false;
    connect(&service,onMutation ? &RecordDirectoryService::mutationBusyChanged : &RecordDirectoryService::busyChanged,this,[&](bool active) {
        if (active && !queued) { queued=true; service.edit(path,id,"second",{}); }
    });
    service.edit(path,id,"first",{});
    QCOMPARE(busy.size(),1);
    QTRY_COMPARE_WITH_TIMEOUT(edited.size(),2,10000);
    QCOMPARE(busy.size(),2);
    QVERIFY(!busy.last()[0].toBool());
    QVERIFY(edited[0][3].toString().isEmpty());
    QVERIFY(edited[1][3].toString().isEmpty());
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    QCOMPARE(repo.list().records.first().displayName,QString("second"));
}

void UiTests::directoryServiceReentrantEnqueue_data()
{
    QTest::addColumn<bool>("onMutation");
    QTest::newRow("busy-notification")<<false;
    QTest::newRow("mutation-notification")<<true;
}

void UiTests::directoryServiceDestroyedByObserver()
{
    QTemporaryDir dir; const auto path=dir.filePath("directory.sqlite");
    RecordIdentities checked;
    { ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
      QVERIFY(repo.save(storedRun("selected")).saved()); checked = repo.identities({}).existing; }
    QPointer<RecordDirectoryService> service=new RecordDirectoryService;
    connect(service,&RecordDirectoryService::namesExported,this,[&](QString,qint64,QString) { delete service.data(); });
    service->exportNames(path,checked,dir.filePath("names.csv"));
    QTRY_VERIFY(service.isNull());
    QVERIFY(QFileInfo::exists(dir.filePath("names.csv")));
}

void UiTests::directoryServiceQueriesAndMutations()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("db.sqlite");
    qint64 id;
    {
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        id = repo.save(storedRun("original")).recordId;
    }
    RecordDirectoryService service;
    int lists = 0, edits = 0;
    RecordList last;
    QString error;
    connect(&service, &RecordDirectoryService::listed, this,
            [&](RecordList list, QStringList, int)
            {
                last = list;
                ++lists;
            });
    connect(&service, &RecordDirectoryService::edited, this,
            [&](QString, qint64, bool, QString e)
            {
                error = e;
                ++edits;
            });
    service.query(path, {}, 0);
    QTRY_COMPARE(lists, 1);
    QCOMPARE(last.records.size(), 1);
    service.edit(path, id, "custom", "memo");
    RecordFilter f;
    f.text = "custom";
    service.query(path, f, 0);
    QTRY_COMPARE(edits, 1);
    QVERIFY(error.isEmpty());
    QTRY_COMPARE(lists, 2);
    QCOMPARE(last.records[0].displayName, QString("custom"));
    service.remove(path, id);
    service.query(path, {}, 0);
    QTRY_COMPARE(edits, 2);
    QTRY_COMPARE(lists, 3);
    QVERIFY(last.records.isEmpty());
}
void UiTests::recordNameExportIncludesUnloadedRows()
{
    QTemporaryDir dir;
    const auto database=dir.filePath("db.sqlite"), csv=dir.filePath("names.csv");
    ResultRepository repo; QVERIFY(repo.open(database).isEmpty());
    for(int i=0;i<105;++i) {
        auto run=storedRun(QString("record %1").arg(i));
        if(i==0) run.provenance["path"]="C:\\data\\中文,\"名字\".csv";
        if(i==104) run.sourceLabel="pair A / B";
        QVERIFY(repo.save(run).saved());
    }
    QCOMPARE(repo.list().records.size(),100); QVERIFY(repo.list().hasMore);
    const auto all = repo.identities({}).existing;
    RecordIdentities checked{{1, all.value(1)}, {105, all.value(105)}};
    const auto first = repo.directoryPage({},100,0);
    QCOMPARE(first.totalMatching,105); QCOMPARE(first.records.size(),100);
    QCOMPARE(first.totalRecords, 105);
    const auto next = repo.directoryPage({}, 100, first.records.last().id);
    QCOMPARE(next.totalMatching, -1);
    QCOMPARE(next.totalRecords, -1);
    RecordFilter filter;
    filter.text = "pair A";
    const auto one = repo.directoryPage(filter, 100, 0);
    QCOMPARE(one.totalMatching, 1);
    QCOMPARE(one.totalRecords, 105);
    filter.text = "absent";
    const auto none = repo.directoryPage(filter, 100, 0);
    QCOMPARE(none.totalMatching, 0);
    QCOMPARE(none.totalRecords, 105);
    RecordDirectoryService service;
    QSignalSpy exported(&service,&RecordDirectoryService::namesExported);
    service.exportNames(database,checked,csv);
    QTRY_COMPARE(exported.size(),1);
    QVERIFY(exported[0][2].toString().isEmpty()); QCOMPARE(exported[0][1].toLongLong(),2);
    QFile output(csv); QVERIFY(output.open(QIODevice::ReadOnly)); const auto bytes=output.readAll(); output.close();
    QCOMPARE(bytes,QByteArray::fromHex("efbbbf") + QString::fromUtf8(
        "ID,Filename\r\n105,\"pair A / B\"\r\n1,\"中文,\"\"名字\"\".csv\"\r\n").toUtf8());
    qint64 count=0;
    CancellationToken cancelled; cancelled.cancel();
    QVERIFY(!repo.exportRecordNames(checked,csv,&count,cancelled).isEmpty()); QCOMPARE(count,0);
    QVERIFY(!repo.exportRecordNames(checked,database).isEmpty());
    QVERIFY(!repo.exportRecordNames(checked,database+"-wal").isEmpty());
    QVERIFY(!repo.exportRecordNames({},csv,&count).isEmpty()); QCOMPARE(count,0);
    checked[1] = QUuid::createUuid();
    QVERIFY(!repo.exportRecordNames(checked,csv,&count).isEmpty()); QCOMPARE(count,0);
    QVERIFY(output.open(QIODevice::ReadOnly)); QCOMPARE(output.readAll(),bytes); output.close();
    QCOMPARE(repo.list(1000).records.size(),105);
    service.exportNames(dir.filePath("missing.sqlite"),{},csv);
    QTRY_COMPARE(exported.size(),2); QVERIFY(!exported[1][2].toString().isEmpty());
    QVERIFY(!QFileInfo::exists(dir.filePath("missing.sqlite")));
}
void UiTests::recordNameExportButtonAndDialog()
{
    QTemporaryDir dir;
    const auto database=dir.filePath("db.sqlite"), csv=dir.filePath("chosen.csv");
    { ResultRepository repo; QVERIFY(repo.open(database).isEmpty());
      QVERIFY(repo.save(storedRun("sample")).saved());
      QVERIFY(repo.save(storedRun("highlighted")).saved()); }
    RecordDirectoryPanel panel; panel.resize(330,700); panel.setDatabase(database); panel.show();
    auto *button=panel.findChild<QPushButton*>("exportRecordNames"); QVERIFY(button);
    auto *list=panel.findChild<QListWidget*>("resultRecords");
    auto *remove=panel.findChild<QPushButton*>("deleteCheckedRecords"); QVERIFY(remove);
    QTRY_COMPARE(list->count(),2);
    list->setCurrentRow(0);
    QVERIFY(!button->isEnabled()); QVERIFY(!remove->isEnabled());
    list->item(1)->setCheckState(Qt::Checked);
    panel.findChild<QLineEdit*>("recordSearch")->setText("highlighted");
    QTRY_COMPARE(list->count(),1); QTRY_VERIFY(button->isEnabled());
    QCOMPARE(panel.checkedRecords().size(),1);
    QSignalSpy messages(&panel,&RecordDirectoryPanel::message);
    const bool wasNativeDisabled=QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
    struct RestoreDialogs { bool value; ~RestoreDialogs() { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,value); } } restore{wasNativeDisabled};
    bool sawDialog=false;
    QTimer closer;
    connect(&closer,&QTimer::timeout,this,[&] {
        if(auto *dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            sawDialog=true;
            if(auto *name=dialog->findChild<QLineEdit*>("fileNameEdit")) name->setText(csv);
            QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
        } else if(auto *notice=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) notice->accept();
    });
    closer.start(20);
    button->click();
    QVERIFY(sawDialog);
    QTRY_VERIFY(QFileInfo::exists(csv));
    QTRY_VERIFY(!messages.isEmpty());
    QTRY_VERIFY(button->isEnabled());
    QFile file(csv); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto content = file.readAll();
    QVERIFY(content.contains("1,\"sample\"")); QVERIFY(!content.contains("highlighted"));
}
void UiTests::nodeActionsAndFrequencyFilter()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("db.sqlite");
    qint64 id;
    {
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        id = repo.save(storedRun("node actions")).recordId;
    }
    ResultsPage page(makeScientificRegistry());
    page.setDatabase(path);
    page.openRecord(id);
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    QTRY_COMPARE(loaded.count(), 1);
    auto *outputs = page.findChild<QTableView *>("resultOutputs");
    QVERIFY(outputs);
    QVERIFY(!page.findChild<QToolButton *>("plotNode_0"));
    QVERIFY(!page.findChild<QToolButton *>("saveNode_0"));
    QTRY_VERIFY(page.plotView()->hasPlot()); // Record clicks now open their default output.
    outputs->selectRow(0);
    page.findChild<QPushButton *>("plotCheckedOutputs")->click();
    QTRY_VERIFY(page.plotView()->hasPlot());
    const auto csv = dir.filePath("node.csv");
    QSignalSpy exported(page.exporter(), &ExportController::finished);
    QVERIFY(page.startCsvExport(csv));
    QTRY_COMPARE(exported.count(), 1);
    QVERIFY(exported.first()[1].toString().isEmpty());
    QFile output(csv);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().contains("Time"));
    page.findChild<QLineEdit *>("recordSearch")->setText("does-not-match");
    QTRY_COMPARE(page.recordCount(), 0);
    page.findChild<QLineEdit *>("recordSearch")->clear();
    QTRY_COMPARE(page.recordCount(), 1);
    page.openRecord(id);
    page.openRecord(id);
    QTRY_COMPARE(page.recordId(), id);
}
void UiTests::closeWaitsForDatabaseEdits()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("close.sqlite");
    qint64 id;
    {
        ResultRepository repo;
        QVERIFY(repo.open(settings.databasePath).isEmpty());
        id = repo.save(storedRun("original")).recordId;
    }
    MainWindow window(settings, false);
    window.show();
    auto *service = window.results()->findChild<RecordDirectoryService *>();
    QVERIFY(service);
    service->edit(settings.databasePath, id, "edited before closing", "preserved");
    QVERIFY(window.results()->databaseBusy());
    window.close();
    QTRY_VERIFY(!window.isVisible());
    ResultRepository repo;
    QVERIFY(repo.open(settings.databasePath).isEmpty());
    QCOMPARE(repo.load(id).displayName, QString("edited before closing"));
}
void UiTests::exactIdSearchLocatesAndOpensConfirmedMatch()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("search.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    auto run = storedRun("oldest.csv");
    for (int i = 1; i <= 106; ++i) {
        run.runId = QUuid::createUuid();
        if (i == 2) run.sourceLabel = "80001.csv";
        if (i > 2) run.sourceLabel = QString("sample-%1.csv").arg(i);
        QVERIFY(repo.save(run).saved());
    }
    ResultsPage page(makeScientificRegistry());
    page.resize(1280, 800);
    page.show();
    page.setDatabase(path);
    auto *directory = page.findChild<RecordDirectoryPanel *>();
    auto *records = page.findChild<QListWidget *>("resultRecords");
    auto *search = page.findChild<QLineEdit *>("recordSearch");
    auto *low = page.findChild<QLineEdit *>("minimumFrequency");
    auto *missing = page.findChild<QLabel *>("recordLookupEmpty");
    auto *matches = page.findChild<QPushButton *>("checkMatchingRecords");
    auto *outputs = page.findChild<QTableView *>("resultOutputs");
    QTRY_COMPARE(records->count(), 100);
    page.openRecord(106);
    QTRY_COMPARE(page.recordId(), 106);
    QTRY_VERIFY(!page.workspace()->loading());
    outputs->selectRow(0);
    const auto checked = page.checkedOutputs();
    QVERIFY(!checked.isEmpty());
    const auto figure = page.workspace()->currentReference().key();
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    search->setText("80001");
    QTRY_COMPARE(records->count(), 1);
    QCOMPARE(records->item(0)->data(RecordRole::Id).toLongLong(), 2);
    low->setText("9");
    QTRY_COMPARE(records->count(), 0);
    search->setText("#1");
    QTRY_COMPARE(records->count(), 1);
    QCOMPARE(records->currentIndex().data(RecordRole::Id).toLongLong(), 1);
    QCOMPARE(page.recordId(), 106);
    QTRY_VERIFY(matches->isEnabled());
    QTest::mouseClick(matches, Qt::LeftButton);
    QTRY_COMPARE(directory->checkedRecords().size(), 1);
    QVERIFY(directory->checkedRecords().contains(1));

    // Changing the query before delivery cancels the pending open.
    QTest::keyClick(search, Qt::Key_Return);
    search->setText("#9000");
    QTest::keyClick(search, Qt::Key_Return);
    QTRY_VERIFY(missing->isVisible());
    QCOMPARE(records->count(), 0);
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(page.recordId(), 106);
    QCOMPARE(page.checkedOutputs().first().key(), checked.first().key());
    QCOMPARE(page.workspace()->currentReference().key(), figure);

    search->setText("#1");
    QTest::keyClick(search, Qt::Key_Return);
    QTRY_COMPARE(page.recordId(), 1);
    QCOMPARE(loaded.count(), 1);
    QVERIFY(!missing->isVisible());
    QVERIFY(page.checkedOutputs().isEmpty());
    search->clear();
    QTRY_COMPARE(records->count(), 0);
    QCOMPARE(low->text(), QString("9"));
    QCOMPARE(page.recordId(), 1);
    low->clear();
    QTRY_COMPARE(records->count(), 100);
    QVERIFY(directory->checkedRecords().contains(1));
}

void UiTests::compactDirectoryAndPeakPeriods()
{
    QTemporaryDir dir; const auto path = dir.filePath("period.sqlite");
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("sine_source", {0,0}).nodeId;
    const auto mtm = model.addChild(root, ChildSlot::Append, "mtm", {{"ar1",false},{"swa",false}}).nodeId;
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    for (const auto &unit : {QString(), QString("1/ka"), QString("1/m")}) {
        auto run = WorkflowExecutor().run({model.freeze().snapshot,root,{"core.csv",{}, {}}});
        auto value = spectrumResult();
        auto spectrum = *dataAs<SpectrumData>(value.outputs["spectrum"]); spectrum.frequencyAxis.unit = unit;
        value.outputs["spectrum"] = makeData(spectrum); run.nodes[mtm] = value; QVERIFY(repo.save(run).saved());
    }
    RecordDirectoryPanel panel; panel.resize(320,560); panel.show(); panel.setDatabase(path);
    auto *list = panel.findChild<QListWidget *>("resultRecords");
    auto *filters = panel.findChild<RecordFilterPanel *>();
    QTRY_COMPARE(panel.recordCount(),3);
    QCOMPARE(list->item(0)->data(RecordRole::FileName).toString(), QString("core.csv"));
    QVERIFY(list->item(0)->toolTip().contains("ID "));
    QVERIFY(list->height() > panel.height()/2);
    const int originalHeight = list->height();
    auto *button = panel.findChild<QToolButton *>("recordFilterButton");
    button->click();
    auto *popup = panel.findChild<QDialog *>("recordFilterPopup");
    QVERIFY(popup->isVisible()); QCOMPARE(list->height(), originalHeight);
    auto *axis = panel.findChild<QComboBox *>("recordPeakAxis");
    auto *unit = panel.findChild<QComboBox *>("frequencyUnit");
    auto *low = panel.findChild<QLineEdit *>("minimumFrequency"), *high = panel.findChild<QLineEdit *>("maximumFrequency");
    QCOMPARE(unit->count(), 3);
    QVERIFY(unit->isVisible());
    QVERIFY(panel.findChild<QLabel *>("peakRangeUnit")->isHidden());
    QCOMPARE(axis->currentIndex(),1); low->setText("9"); high->setText("11");
    QTRY_COMPARE(panel.recordCount(),1);
    QVERIFY(filters->filter().frequencyUnit.isEmpty()); QCOMPARE(list->item(0)->data(RecordRole::Id).toLongLong(),1LL);
    QCOMPARE(filters->filter().minFrequency,1./11); QCOMPARE(filters->filter().maxFrequency,1./9);
    unit->setCurrentIndex(unit->findData("1/ka")); QCOMPARE(unit->currentText(),QString("ka"));
    QTRY_COMPARE(list->item(0)->data(RecordRole::Id).toLongLong(),2LL);
    unit->setCurrentIndex(unit->findData("1/m")); QCOMPARE(unit->currentText(),QString("m"));
    QTRY_COMPARE(list->item(0)->data(RecordRole::Id).toLongLong(),3LL);
    low->clear(); QCOMPARE(filters->filter().maxFrequency,-1.);
    axis->setCurrentIndex(0); QCOMPARE(filters->filter().maxFrequency,-1.); QCOMPARE(filters->filter().minFrequency,1./11);
    axis->setCurrentIndex(1); low->setText("0"); QVERIFY(!filters->filter().validate().isEmpty());
    low->setText("12"); QVERIFY(!filters->filter().validate().isEmpty());
    low->setText("10"); high->setText("10"); QVERIFY(filters->filter().validate().isEmpty());
    const auto resets = popup->findChildren<QPushButton *>();
    for (auto *reset : resets)
        if (reset->text() == text("ui.reset_filters")) {
            axis->setCurrentIndex(0);
            reset->click();
            QCOMPARE(axis->currentIndex(),1);
            QVERIFY(!filters->filter().hasFrequency() && filters->filter().frequencyUnit.isEmpty());
        }
    popup->reject(); QVERIFY(!popup->isVisible()); QVERIFY(list->height() > panel.height()/2);

    // An active range must be requeried when the next database has a single different unit.
    const auto other = dir.filePath("single.sqlite");
    ResultRepository single;
    QVERIFY(single.open(other).isEmpty());
    auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {"single.csv", {}, {}}});
    run.nodes[mtm] = spectrumResult(); // 1/ka, one indexed peak at 0.1
    QVERIFY(single.save(run).saved());
    unit->setCurrentIndex(unit->findData("1/m"));
    low->setText("9"); high->setText("11");
    panel.setDatabase(other);
    QTRY_COMPARE(filters->filter().frequencyUnit, QString("1/ka"));
    QTRY_COMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(RecordRole::FileName).toString(), QString("single.csv"));
    QVERIFY(unit->isHidden());
    auto *suffix = panel.findChild<QLabel *>("peakRangeUnit");
    QVERIFY(!suffix->isHidden());
    QCOMPARE(suffix->text(), QString("ka"));
    axis->setCurrentIndex(0);
    QCOMPARE(suffix->text(), QString("1/ka"));
    QCOMPARE(filters->filter().minFrequency, 1./11);
    QCOMPARE(filters->filter().maxFrequency, 1./9);
}
void UiTests::peakFilterLeavesFigureIndependent_data()
{
    QTest::addColumn<QString>("theme");
    QTest::addColumn<QString>("frequencyUnit");
    QTest::newRow("light-unitless") << QString("light") << QString("");
    QTest::newRow("dark-declared-unit") << QString("dark") << QString("1/ka");
}

void UiTests::peakFilterLeavesFigureIndependent()
{
    QFETCH(QString, theme);
    QFETCH(QString, frequencyUnit);
    const auto previousTheme = ThemeManager::instance().name();
    QVERIFY(ThemeManager::instance().apply(*qApp, theme).isEmpty());
    auto restoreTheme = qScopeGuard([&] { ThemeManager::instance().apply(*qApp, previousTheme); });
    QTemporaryDir dir;
    const auto path = dir.filePath("frequency.sqlite");
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("sine_source", {0, 0}).nodeId;
    const auto node = model.addChild(root, ChildSlot::Down, "mtm").nodeId;
    auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {"dense peaks", {}, {}}});
    SpectrumData spectrum;
    spectrum.frequencyAxis = {"Frequency", frequencyUnit};
    spectrum.powerAxis = {"Power", "PSD"};
    TableData peaks{{"frequency", "power"}, {}};
    for (int i = 0; i <= 10000; ++i) {
        const double f = i / 200.;
        const double power = i == 1 ? 8. : (i % 2 ? .2 : .1);
        spectrum.frequency.append(f);
        spectrum.power.append(power);
        if (i % 2) peaks.rows.append({f, power});
    }
    run.nodes[node] = NodeResult::success({{"spectrum", makeData(spectrum)}, {"peaks", makeData(peaks)}});
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    const auto saved = repo.save(run);
    QVERIFY(saved.saved());
    QVERIFY(repo.save(storedRun("no peak table")).saved());

    ResultsPage page(makeScientificRegistry());
    page.resize(1450, 900);
    page.show();
    page.setDatabase(path);
    QTRY_COMPARE(page.recordCount(), 2);
    auto *unit = page.findChild<QComboBox *>("frequencyUnit");
    QTRY_VERIFY(unit->findData(frequencyUnit) >= 0);
    QVERIFY(unit->isHidden());
    auto *suffix = page.findChild<QLabel *>("peakRangeUnit");
    QCOMPARE(suffix->isHidden(), frequencyUnit.isEmpty());
    unit->setCurrentIndex(unit->findData(frequencyUnit));
    auto *high = page.findChild<QLineEdit *>("maximumFrequency");
    QCOMPARE(page.findChild<QComboBox *>("recordPeakAxis")->currentIndex(), 1);
    high->setText("1"); // Period <= 1 selects indexed frequencies >= 1.
    QTRY_COMPARE(page.recordCount(), 1);
    page.openRecord(saved.recordId);
    QTRY_COMPARE(page.recordId(), saved.recordId);
    QTRY_VERIFY(page.plotView()->hasPlot() && !page.workspace()->loading());
    auto *outputs = page.findChild<QTableView *>("resultOutputs");
    auto *outputViewport = outputs->viewport();
    const QRect first = outputs->visualRect(outputs->model()->index(0, 0));
    const QRect second = outputs->visualRect(outputs->model()->index(1, 0));
    const auto fill = [outputViewport](const QRect &rect) {
        return viewColorAt(outputViewport, QPoint(rect.right() - 15, rect.center().y()));
    };
    QTest::mouseMove(outputViewport, first.center());
    QTest::mouseClick(outputViewport, Qt::LeftButton, Qt::NoModifier, first.center());
    QTest::mouseMove(outputViewport, second.center());
    QTRY_COMPARE(fill(second), themeColor("fileListHover"));
    QCOMPARE(fill(first), themeColor("fileListSelection"));
    QCOMPARE(viewColorAt(outputViewport, {first.left() + 6, first.center().y()}), themeColor("fileListMark"));
    auto *detail = page.findChild<QTableWidget *>("resultSteps")->item(1, 2);
    QCOMPARE(detail->text(), QString("2"));
    QCOMPARE(detail->toolTip(), text("ui.from_step_1_2").arg(1).arg(portLabel("series")));

    auto *view = page.plotView();
    auto *plot = view->plotWidget();
    const auto backgroundAt = [&](double x) {
        plot->replot();
        return viewColorAt(plot, QPoint(qRound(plot->xAxis->coordToPixel(x)),
                                        qRound(plot->yAxis->coordToPixel(5.37))));
    };
    QVERIFY(plot->xAxis->range().upper >= 50.);
    QCOMPARE(backgroundAt(24.37), view->style().background);
    auto *tabs = page.findChild<QTabWidget *>("plotStyleSections");
    tabs->setCurrentIndex(1);
    auto *manual = tabs->findChild<QCheckBox *>("axisManual");
    QVERIFY(!manual->isChecked());
    manual->click();
    auto *lower = tabs->findChild<QLineEdit *>("axisLower");
    auto *upper = tabs->findChild<QLineEdit *>("axisUpper");
    QSignalSpy refined(view, &PlotView::displayDataChanged);
    const auto enter = [](QLineEdit *edit, const char *number) {
        edit->setFocus();
        edit->selectAll();
        QTest::keyClicks(edit, number);
        QTest::keyClick(edit, Qt::Key_Return);
    };
    enter(lower, "0");
    enter(upper, "1");
    QTRY_COMPARE(plot->xAxis->range().lower, 0.);
    QTRY_COMPARE(plot->xAxis->range().upper, 1.);
    QTRY_VERIFY(!refined.isEmpty() && !page.workspace()->loading());
    QVERIFY(manual->isChecked());
    QCOMPARE(plot->xAxis->range().lower, 0.);
    QCOMPARE(plot->xAxis->range().upper, 1.);
    QCOMPARE(backgroundAt(.637), view->style().background);
    page.selectStep(0, true);
    QTRY_COMPARE(page.workspace()->currentReference().nodeId, root);
    QTRY_VERIFY(!page.workspace()->loading());
    page.selectStep(1, true);
    QTRY_COMPARE(page.workspace()->currentReference().nodeId, node);
    QTRY_VERIFY(!page.workspace()->loading());
    // Returning to a saved figure page recreates its on-screen plot widget.
    QTRY_VERIFY(page.plotView() && page.plotView()->hasPlot());
    view = page.plotView();
    plot = view->plotWidget();
    QVERIFY(manual->isChecked());
    QCOMPARE(plot->xAxis->range().lower, 0.);
    QCOMPARE(plot->xAxis->range().upper, 1.);
    manual->click();
    QTRY_VERIFY(plot->xAxis->range().upper >= 50.);
    QCOMPARE(backgroundAt(24.37), view->style().background);
    high->setText(".001");
    QTRY_COMPARE(page.recordCount(), 0);
    high->clear();
    QTRY_COMPARE(page.recordCount(), 2);
    QCOMPARE(backgroundAt(24.37), view->style().background);

    auto *ports = page.findChild<QComboBox *>("outputPort");
    ports->setCurrentIndex(ports->findData("peaks"));
    QSignalSpy exported(page.exporter(), &ExportController::finished);
    const auto csv = dir.filePath("peaks.csv");
    QVERIFY(page.startCsvExport(csv));
    QTRY_COMPARE(exported.size(), 1);
    QVERIFY(exported.first()[1].toString().isEmpty());
    QFile contents(csv);
    QVERIFY(contents.open(QIODevice::ReadOnly));
    QCOMPARE(contents.readAll().trimmed().split('\n').size(), peaks.rows.size() + 1);
}

void UiTests::parameterLayoutDefaultsAndVisibility()
{
    const auto registry=makeScientificRegistry();
    for(const auto* language:{"zh","en"}) {
        setLanguage(language);
        for(const auto& d:registry.descriptors()) {
            if(d.key=="file_source")continue; // Dedicated importer owns these controls.
            NodeDefinition n;n.id=QUuid::createUuid();n.method=d.key;
            n.parameters=registry.normalize(d.key,{}).parameters;
            ParameterEditor editor;editor.resize(300,740);editor.showNode(&n,registry);editor.show();
            QCoreApplication::processEvents();
            QCOMPARE(editor.parameters(),n.parameters);
            QVERIFY(!editor.dirty());
            if(!d.parameters.isEmpty()) {
                auto *reset=editor.findChild<QPushButton*>("resetNodeDefaults"); QVERIFY(reset);
                QTest::mouseClick(reset,Qt::LeftButton);
                QCOMPARE(editor.parameters(),registry.normalize(d.key,{}).parameters);
            }
            if(d.key=="detrend_lowess") {
                auto* label=editor.findChild<QLabel*>("parameterLabel_fraction");QVERIFY(label);
                QVERIFY2(label->width()>200,qPrintable(QString::number(label->width())));
                QVERIFY(label->height()<2*label->fontMetrics().height());
                const auto capture=qEnvironmentVariable("ASTER_CAPTURE_DIR");
                if(!capture.isEmpty()){QDir().mkpath(capture);editor.grab().save(capture+"/lowess_"+language+".png");}
            }
        }
        NodeDefinition n;n.id=QUuid::createUuid();n.method="seasonal_insolation";
        n.parameters=registry.normalize(n.method,{}).parameters;
        ParameterEditor editor;editor.resize(340,900);editor.showNode(&n,registry);editor.show();
        auto* mode=editor.findChild<QComboBox*>("parameter_season_basis");QVERIFY(mode);
        auto* month=editor.findChild<QWidget*>("parameterRow_month_start");
        auto* longitude=editor.findChild<QWidget*>("parameterRow_season_start_deg");QVERIFY(month&&longitude);
        QVERIFY(month->isHidden());QVERIFY(!longitude->isHidden());
        mode->setCurrentIndex(mode->findData("calendar_365"));
        QVERIFY(!month->isHidden());QVERIFY(longitude->isHidden());
        QCOMPARE(editor.parameters()["month_start"].toInt(),6);
        QCOMPARE(editor.parameters()["day_start"].toInt(),1);
        QCOMPARE(editor.parameters()["season_start_deg"].toDouble(),90.);
        QSignalSpy applied(&editor,&ParameterEditor::applyRequested);
        QVERIFY(!editor.findChild<QPushButton*>("applyParameters"));
        QTRY_COMPARE(applied.size(),1);
        const auto capture=qEnvironmentVariable("ASTER_CAPTURE_DIR");
        if(!capture.isEmpty()){QCoreApplication::processEvents();editor.grab().save(capture+"/season_"+language+".png");}
    }
    setLanguage("zh");
}
void UiTests::menuTouchTargetsAndPeakMarkers()
{
    TaskCatalog catalog(makeScientificRegistry());
    auto* menu=catalog.menu(&catalog,[](const TaskDescriptor& d){return bool(d.inputKind);},[](QString){});
    QList<QMenu*> menus=menu->findChildren<QMenu*>();menus.prepend(menu);
    for(auto* m:menus) {
        QVERIFY(m->minimumWidth()>=240);QVERIFY(m->font().pointSizeF()>=11);
        m->ensurePolished();m->adjustSize();
        for(auto* a:m->actions())if(!a->isSeparator())QVERIFY(m->actionGeometry(a).height()>=34);
    }
    QVERIFY(findAction(menu,"clean_series"));QVERIFY(findAction(menu,"mtm"));delete menu;
    PlotView view;view.resize(900,500);view.show();view.showOutput(spectrumResult(),"spectrum");
    QVERIFY(view.style().curves.value("peaks").visible);
    auto* plot=view.findChild<QCustomPlot*>("resultPlot");QVERIFY(plot);bool visible=false;
    for(int i=0;i<plot->graphCount();++i)if(plot->graph(i)->property("outputKey").toString()=="peaks")visible=plot->graph(i)->visible();
    QVERIFY(visible);
}
QTEST_MAIN(UiTests)
#include "ui_tests.moc"

void UiTests::previewRunsOnlyAncestorsAndSelectedInput()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("input_series").nodeId;
    const auto mid = doc.addChild(root, ChildSlot::Right, "affine", {{"factor", 2}}).nodeId;
    const auto target = doc.addChild(mid, ChildSlot::Down, "affine", {{"offset", 7}}).nodeId;
    const auto sibling = doc.addChild(root, ChildSlot::Down, "series_summary").nodeId;
    const auto other = doc.addRoot("sine_source").nodeId;
    SourceItem input{"one", series(), {}};
    doc.setSources(root, {input, {"two", makeData(SeriesData{{0,1},{9,9},{}, {}}), {}}});
    const auto plan = prepareNodePreview(doc.model(), target, root, input);
    QVERIFY(plan.validation.ok()); QCOMPARE(plan.requests.size(), 1);
    const auto preview = WorkflowExecutor().run(plan.requests.front());
    const auto full = WorkflowExecutor().run(doc.plan().requests.front());
    QCOMPARE(preview.state, RunState::Succeeded);
    QCOMPARE(preview.executionOrder, QVector<NodeId>({root,mid,target}));
    QVERIFY(!preview.nodes.contains(sibling)); QVERIFY(!preview.nodes.contains(other));
    const auto *a = dataAs<SeriesData>(preview.nodes[target].outputs["series"]);
    const auto *b = dataAs<SeriesData>(full.nodes[target].outputs["series"]);
    QVERIFY(a && b); QCOMPARE(a->x,b->x); QCOMPARE(a->y,b->y);
    QCOMPARE(a->y,QVector<double>({9,15,11,17}));
    QVERIFY(!prepareNodePreview(doc.model(), target, other, input).validation.ok());
    QCOMPARE(doc.model().definition().nodes.size(),5);
}
void UiTests::previewUsesTheSelectedParentPort()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("sine_source").nodeId;
    const auto trend = doc.addChild(root, ChildSlot::Down, "linear_detrend").nodeId;
    const auto *entry = doc.model().registry().find("linear_detrend");
    QVERIFY(entry && entry->descriptor.outputs.size()>1);
    QString alternative;
    for (const auto &p:entry->descriptor.outputs)
        if(p.key != entry->descriptor.defaultOutput && p.kind==DataKind::Series)alternative=p.key;
    QVERIFY(!alternative.isEmpty());
    const auto child = doc.addChild(trend,ChildSlot::Down,"affine",{{"factor",3}},alternative).nodeId;
    const auto plan = prepareNodePreview(doc.model(),child,{},{}); QVERIFY(plan.validation.ok());
    const auto run = WorkflowExecutor().run(plan.requests.front());
    QCOMPARE(run.state,RunState::Succeeded);
    const auto *a=dataAs<SeriesData>(run.nodes[trend].outputs[alternative]);
    const auto *b=dataAs<SeriesData>(run.nodes[child].outputs["series"]); QVERIFY(a&&b);
    for(int i=0;i<a->y.size();++i)QCOMPARE(b->y[i],a->y[i]*3);
}

namespace
{
PlotView *generatedSourcePlot(WorkflowPreviewPanel &panel)
{
    // Identify the upper plot without requiring a test-only widget name.
    for (auto *plot : panel.findChildren<PlotView *>())
        if (plot->objectName() != "nodePreviewPlot") return plot;
    return nullptr;
}
QString sourceCardTooltip(const WorkflowCanvas &canvas, NodeId source)
{
    for (auto *item : canvas.scene()->items())
        if (item->data(0).toInt() == 1 && item->data(1).toUuid() == source)
            return item->toolTip();
    return {};
}
}
void UiTests::generatedSourcePreviewFollowsBranch()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("precession_tilt_source").nodeId;
    const auto tilt = doc.addChild(root, ChildSlot::Down, "mtm",
                                  {{"ar1", false}, {"swa", false}}, "obliquity").nodeId;
    const auto mid = doc.addChild(root, ChildSlot::Right, "affine", {}, "precession").nodeId;
    const auto deep = doc.addChild(mid, ChildSlot::Down, "affine").nodeId;
    QVERIFY(!root.isNull() && !tilt.isNull() && !deep.isNull());
    WorkflowPreviewPanel panel(&doc); panel.resize(440, 850); panel.show();
    auto *controller = panel.controller();
    auto *source = generatedSourcePlot(panel);
    auto *output = panel.findChild<PlotView *>("nodePreviewPlot");
    QVERIFY(source && output);
    for (const auto &selection : {qMakePair(tilt, QString("obliquity")),
                                 qMakePair(deep, QString("precession")),
                                 qMakePair(tilt, QString("obliquity"))})
    {
        controller->selectNode(selection.first);
        QVERIFY(!source->hasOutput()); // Do not show a previous branch while waiting.
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(), WorkflowPreviewController::State::Ready, 15000);
        QCOMPARE(source->currentPort(), selection.second);
        const auto actual = encodeData(source->currentData());
        const auto expected = encodeData(controller->result()->original.outputs[selection.second]);
        QVERIFY(actual.ok() && expected.ok()); QCOMPARE(actual.bytes, expected.bytes);
        if (selection.first == tilt)
        {
            QCOMPARE(dataAs<SeriesData>(source->currentData())->yAxis.unit, QString("deg"));
            const auto plan = prepareNodePreview(doc.model(), tilt, {}, {});
            QVERIFY(plan.validation.ok());
            const auto run = WorkflowExecutor().run(plan.requests.front());
            QCOMPARE(encodeData(output->currentData()).bytes,
                     encodeData(run.nodes[tilt].outputs["spectrum"]).bytes);
        }
    }
    // A new connection on the same ancestor path must also change the source plot.
    auto definition = doc.model().definition();
    for (auto &node : definition.nodes)
        if (node.id == mid) node.input->output = "axial_precession";
    QVERIFY(doc.replaceDefinition(definition).ok());
    controller->selectNode(deep);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(), WorkflowPreviewController::State::Ready, 15000);
    QCOMPARE(source->currentPort(), QString("axial_precession"));
    QCOMPARE(dataAs<SeriesData>(source->currentData())->yAxis.unit, QString("rad"));
    // Rapid selection changes must not deliver an obsolete source plot.
    controller->selectNode(tilt); controller->selectNode(deep); controller->selectNode(tilt);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(), WorkflowPreviewController::State::Ready, 15000);
    QCOMPARE(source->currentPort(), QString("obliquity"));
}
void UiTests::generatedSourcePreviewKeepsOutputChoice()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("precession_tilt_source", {{"orbit_end_ka", 100}}).nodeId;
    const auto child = doc.addChild(root, ChildSlot::Down, "affine", {}, "obliquity").nodeId;
    WorkflowPreviewPanel panel(&doc); panel.resize(440, 850); panel.show();
    auto *controller = panel.controller();
    auto *source = generatedSourcePlot(panel);
    auto *output = panel.findChild<PlotView *>("nodePreviewPlot");
    auto *ports = panel.findChild<QComboBox *>("previewOutput");
    QVERIFY(source && output && ports);
    controller->selectNode(root);
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QSignalSpy started(controller, &WorkflowPreviewController::started);
    for (const auto *port : {"obliquity", "precession", "axial_precession"})
    {
        const int index = ports->findData(port); QVERIFY(index >= 0);
        ports->setCurrentIndex(index);
        QCOMPARE(source->currentPort(), QString(port));
        QCOMPARE(encodeData(source->currentData()).bytes, encodeData(output->currentData()).bytes);
    }
    QCOMPARE(started.count(), 0); // Viewing another output is not a new calculation.
    controller->retry();
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QCOMPARE(ports->currentData().toString(), QString("axial_precession"));
    QCOMPARE(source->currentPort(), QString("axial_precession"));
    controller->selectNode(child);
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QCOMPARE(source->currentPort(), QString("obliquity")); // Bound port overrides the root's view choice.
    controller->selectNode(root);
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QCOMPARE(ports->currentData().toString(), QString("axial_precession"));
    QVERIFY(doc.setParameters(root, {{"orbit_end_ka", 80}}).ok());
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QCOMPARE(source->currentPort(), QString("axial_precession"));
    QCOMPARE(dataAs<SeriesData>(source->currentData())->x.back(), 80.);
    panel.setSuspended(true); QVERIFY(!source->hasOutput());
    panel.setSuspended(false);
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QCOMPARE(source->currentPort(), QString("axial_precession"));
}
void UiTests::generatedSourcePreviewCanvasRoundTrip()
{
    QTemporaryDir dir; AppSettings settings; settings.databasePath=dir.filePath("source.sqlite");
    MainWindow window(settings,false); window.resize(1600,950); window.show();
    auto *doc=window.document();
    const auto root=doc->addRoot("precession_tilt_source",{{"orbit_end_ka",100.}}).nodeId;
    auto *panel=window.findChild<WorkflowPreviewPanel*>(); QVERIFY(panel);
    auto *controller=panel->controller(); auto *source=generatedSourcePlot(*panel);
    auto *output=panel->findChild<PlotView*>("nodePreviewPlot");
    auto *ports=panel->findChild<QComboBox*>("previewOutput"); QVERIFY(source&&output&&ports);
    window.canvas()->nodeSelected(root);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,10000);
    ports->setCurrentIndex(ports->findData("obliquity"));
    QCOMPARE(source->currentPort(),QString("obliquity"));
    const auto tilt=doc->addChild(root,ChildSlot::Down,"mtm",{{"ar1",false},{"swa",false}},"obliquity").nodeId;
    const auto prec=doc->addChild(root,ChildSlot::Right,"mtm",{{"ar1",false},{"swa",false}},"precession").nodeId;
    QVERIFY(!tilt.isNull()&&!prec.isNull());
    QSignalSpy edits(doc,&WorkflowDocument::changed);
    for (const auto &selection : {qMakePair(tilt,QString("obliquity")),qMakePair(root,QString("obliquity")),
                                 qMakePair(prec,QString("precession")),qMakePair(root,QString("obliquity"))}) {
        window.canvas()->nodeSelected(selection.first);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,10000);
        QCOMPARE(source->currentPort(),selection.second);
        QCOMPARE(encodeData(source->currentData()).bytes,
                 encodeData(controller->result()->original.outputs[selection.second]).bytes);
        if(selection.first==root) QCOMPARE(output->currentPort(),QString("obliquity"));
    }
    QCOMPARE(edits.count(),0);
    QCOMPARE(doc->node(tilt)->input->output,QString("obliquity"));
    QCOMPARE(doc->node(prec)->input->output,QString("precession"));
}
void UiTests::generatedSourcePreviewSurvivesDownstreamFailure()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("precession_tilt_source", {{"orbit_end_ka", 100}}).nodeId;
    const auto child = doc.addChild(root, ChildSlot::Down, "coco_scan", {}, "obliquity").nodeId;
    WorkflowPreviewPanel panel(&doc); panel.resize(440, 850); panel.show();
    auto *controller = panel.controller();
    auto *source = generatedSourcePlot(panel);
    auto *output = panel.findChild<PlotView *>("nodePreviewPlot");
    QVERIFY(source && output);
    controller->selectNode(child);
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Failed);
    QCOMPARE(controller->result()->failedNode, child);
    QCOMPARE(controller->result()->output.diagnostic().code, QString("CHRONOLOGY_UNIT"));
    QVERIFY(!output->hasOutput());
    QVERIFY(source->hasOutput());
    QCOMPARE(source->currentPort(), QString("obliquity"));
    QCOMPARE(encodeData(source->currentData()).bytes,
             encodeData(controller->result()->original.outputs["obliquity"]).bytes);
    // A failure in the source itself must not leave the previous source visible.
    QVERIFY(doc.setParameters(root, {{"orbit_end_ka", 0}}).ok());
    QVERIFY(!source->hasOutput());
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Failed);
    QCOMPARE(controller->result()->failedNode, root);
    QVERIFY(!source->hasOutput());
    QVERIFY(doc.setParameters(root, {{"orbit_end_ka", 80}}).ok());
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Failed);
    QCOMPARE(controller->result()->failedNode, child);
    QVERIFY(source->hasOutput());
    QCOMPARE(dataAs<SeriesData>(source->currentData())->x.back(), 80.);
}
void UiTests::sourceCardFollowsSelectedBranch()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("precession_tilt_source").nodeId;
    const auto a = doc.addChild(root, ChildSlot::Down, "affine", {}, "obliquity").nodeId;
    const auto b = doc.addChild(root, ChildSlot::Right, "affine", {}, "precession").nodeId;
    const auto deep = doc.addChild(a, ChildSlot::Down, "affine").nodeId;
    WorkflowCanvas canvas(&doc);
    const auto &descriptor = doc.model().registry().find("precession_tilt_source")->descriptor;
    for (const auto &selection : {qMakePair(a, QString("obliquity")),
                                 qMakePair(b, QString("precession")),
                                 qMakePair(deep, QString("obliquity"))})
    {
        canvas.setSelectedNode(selection.first);
        QCOMPARE(sourceCardTooltip(canvas, root), "#1 · " + methodLabel(descriptor) + "\n" +
                 portLabel(descriptor, selection.second));
    }
    // No active branch: describe all available outputs instead of implying
    // that every descendant consumes the source's default output.
    canvas.setSelectedNode(root);
    const auto description = sourceCardTooltip(canvas, root);
    for (const auto &port : descriptor.outputs) QVERIFY(description.contains(portLabel(descriptor, port.key)));
    const auto sine = doc.addRoot("sine_source").nodeId;
    const auto trend = doc.addChild(sine, ChildSlot::Down, "linear_detrend").nodeId;
    const auto &transform = doc.model().registry().find("linear_detrend")->descriptor;
    QString alternative;
    for (const auto &port : transform.outputs)
        if (port.key != transform.defaultOutput && port.kind == DataKind::Series) alternative = port.key;
    QVERIFY(!alternative.isEmpty());
    const auto child = doc.addChild(trend, ChildSlot::Down, "affine", {}, alternative).nodeId;
    QVERIFY(!child.isNull());
    canvas.setSelectedNode(child);
    QCOMPARE(sourceCardTooltip(canvas, trend), "#6 · " + methodLabel(transform) + "\n" +
             portLabel(transform, alternative) + "\n" + text("ui.replace_node_hint"));
    // The paired inputs bypass the placement parent's obliquity branch.
    // Display only the two actual referenced ports, not that unused branch port.
    const auto joint = doc.addChild(a, ChildSlot::Right, "cross_spectrum").nodeId;
    QVERIFY(!joint.isNull());
    const auto dataset = doc.datasets(root).front().id;
    QVERIFY(doc.setPairs(joint, {{QUuid::createUuid(), "A/B",
        {dataset, "axial_precession", root}, {dataset, "precession", root}}}).isEmpty());
    canvas.setSelectedNode(joint);
    QCOMPARE(sourceCardTooltip(canvas, root), "#1 · " + methodLabel(descriptor) + "\n" +
        portLabel(descriptor, "axial_precession") + " · " + portLabel(descriptor, "precession"));
}

void UiTests::generatedSourcePreviewHandlesTableOutputs()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("milankovitch_period").nodeId;
    QVERIFY(!root.isNull());
    WorkflowPreviewPanel panel(&doc); panel.resize(440, 850); panel.show();
    auto *source = generatedSourcePlot(panel);
    auto *output = panel.findChild<PlotView *>("nodePreviewPlot");
    auto *ports = panel.findChild<QComboBox *>("previewOutput");
    QVERIFY(source && output && ports);
    panel.controller()->selectNode(root);
    QTRY_COMPARE(panel.controller()->state(), WorkflowPreviewController::State::Ready);
    QVERIFY(ports->count() > 1);
    QSignalSpy started(panel.controller(), &WorkflowPreviewController::started);
    for (int i = 0; i < ports->count(); ++i)
    {
        ports->setCurrentIndex(i);
        QVERIFY(dataAs<TableData>(source->currentData()));
        QCOMPARE(source->currentPort(), ports->currentData().toString());
        QCOMPARE(encodeData(source->currentData()).bytes, encodeData(output->currentData()).bytes);
    }
    QCOMPARE(started.count(), 0);
}

void UiTests::previewCancelsStaleWorkAndKeepsUiResponsive()
{
    auto registry=makeScientificRegistry();
    TaskDescriptor d; d.key="slow_preview";d.label="Slow";d.category={"Test"};d.outputs={{"series","Series",DataKind::Series}};d.defaultOutput="series";
    QVERIFY(registry.add(d,[](const QJsonObject&){return std::make_unique<SlowSource>();}).ok());
    WorkflowDocument doc(registry);const auto slow=doc.addRoot(d.key).nodeId;
    const auto fast=doc.addRoot("sine_source",{{"count",16}}).nodeId;
    WorkflowPreviewController controller(&doc);controller.setActive(true);controller.selectNode(slow);
    QTRY_COMPARE(controller.state(),WorkflowPreviewController::State::Running);
    int pulses=0;QTimer pulse;pulse.setInterval(5);connect(&pulse,&QTimer::timeout,[&]{++pulses;});pulse.start();
    // Timer delivery can be coalesced on a busy CI host. Require event-loop
    // progress while the slow worker is still running, not a fixed cadence.
    QTRY_VERIFY_WITH_TIMEOUT(pulses>=3,2000);
    QCOMPARE(controller.state(),WorkflowPreviewController::State::Running);
    controller.selectNode(fast);
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(),WorkflowPreviewController::State::Ready,3000);
    QCOMPARE(controller.node(),fast);QCOMPARE(controller.result()->executed,QVector<NodeId>({fast}));
    QTest::qWait(80);QCOMPARE(controller.node(),fast);
    const auto *data=dataAs<SeriesData>(controller.result()->output.outputs["series"]);QVERIFY(data);QCOMPARE(data->x.size(),16);
}
void UiTests::previewIgnoresUnrelatedEditsAndEmptyClicks()
{
    WorkflowDocument doc(makeScientificRegistry());const auto root=doc.addRoot("sine_source").nodeId;
    const auto child=doc.addChild(root,ChildSlot::Down,"affine").nodeId;
    WorkflowPreviewController c(&doc);QSignalSpy started(&c,&WorkflowPreviewController::started);
    c.setActive(true);c.selectNode(child);QTRY_COMPARE(c.state(),WorkflowPreviewController::State::Ready);
    const auto result=c.result();const auto other=doc.addRoot("sine_source").nodeId;
    doc.setParameters(other,{{"count",32}});c.selectNode({});c.selectNode(QUuid::createUuid());c.selectNode(child);
    QTest::qWait(400);QCOMPARE(started.count(),1);QCOMPARE(c.result(),result);
    doc.setParameters(root,{{"count",32}});QTRY_COMPARE(c.state(),WorkflowPreviewController::State::Ready);
    QCOMPARE(started.count(),2);QCOMPARE(dataAs<SeriesData>(c.result()->output.outputs["series"])->x.size(),32);
    c.setEditing(true);QVERIFY(!c.result());QTest::qWait(300);QCOMPARE(started.count(),2);
    doc.setParameters(child,{{"factor",2}});doc.setParameters(child,{{"factor",3}});c.setEditing(false);
    QTRY_COMPARE(c.state(),WorkflowPreviewController::State::Ready);QCOMPARE(started.count(),3);
}
void UiTests::previewUsesDraftFileOptionsAndAutoParameters()
{
    QTemporaryDir dir;auto path=dir.filePath("three.csv");QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("time,A,B\n0,1,10\n1,2,20\n2,3,30\n");f.close();
    AppSettings settings;settings.databasePath=dir.filePath("preview.sqlite");
    MainWindow window(settings,false);window.resize(1550,950);window.show();
    auto *sources=window.findChild<SourcePanel*>();sources->addFiles({path});
    QTRY_VERIFY(sources->readyToRun());const auto root=sources->root();
    const auto child=window.document()->addChild(root,ChildSlot::Right,"affine",{{"factor",2}}).nodeId;
    // Use the canvas's public selection signal: catalog highlight is not a target.
    window.canvas()->nodeSelected(child);
    auto *preview=window.findChild<WorkflowPreviewPanel*>();auto *c=preview->controller();
    QTRY_COMPARE(c->state(),WorkflowPreviewController::State::Ready);
    QCOMPARE(dataAs<SeriesData>(c->result()->output.outputs["series"])->y,QVector<double>({2,4,6}));
    auto *options=sources->findChild<FileOptionsEditor*>();
    auto *y=options->findChild<QComboBox*>("fileYColumn");QVERIFY(y);y->setCurrentIndex(y->findData(2));
    QTRY_COMPARE(c->state(),WorkflowPreviewController::State::Ready);
    QCOMPARE(dataAs<SeriesData>(c->result()->output.outputs["series"])->y,QVector<double>({20,40,60}));
    // No Apply click. Draft is the preview input; normal Run flushes exactly the same options.
    auto *factor=window.findChild<ParameterEditor*>()->findChild<QDoubleSpinBox*>("parameter_factor");QVERIFY(factor);
    factor->setValue(4);factor->setValue(5);factor->setValue(3);
    QTRY_COMPARE(effectiveParameters(*window.document()->node(child),window.document()->datasets(child).front().id)["factor"].toDouble(),3.);
    QTRY_COMPARE(c->state(),WorkflowPreviewController::State::Ready);
    QCOMPARE(dataAs<SeriesData>(c->result()->output.outputs["series"])->y,QVector<double>({30,60,90}));
    QSignalSpy started(c,&WorkflowPreviewController::started);
    QTest::mouseClick(window.canvas()->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(12,12));
    QTest::qWait(400);QCOMPARE(started.count(),0);QCOMPARE(c->node(),child);
    QVERIFY(sources->applyCurrentOptions());
    QTRY_COMPARE(c->state(),WorkflowPreviewController::State::Ready);
    const auto run=WorkflowExecutor().run(window.document()->plan().requests.front());
    QCOMPARE(dataAs<SeriesData>(run.nodes[child].outputs["series"])->y,
             dataAs<SeriesData>(c->result()->output.outputs["series"])->y);
    QFile replacement(path);QVERIFY(replacement.open(QIODevice::WriteOnly|QIODevice::Truncate));
    replacement.write("time,A,B\n0,1,100\n1,2,200\n2,3,300\n");replacement.close();
    c->retry();QTRY_COMPARE(c->state(),WorkflowPreviewController::State::Ready);
    QCOMPARE(dataAs<SeriesData>(c->result()->output.outputs["series"])->y,QVector<double>({300,600,900}));
    auto *raw=preview->findChild<QCustomPlot*>("filePreviewPlot");QVERIFY(raw);
    auto *rawCurve=qobject_cast<QCPCurve*>(raw->plottable(0));QVERIFY(rawCurve);
    QCOMPARE(rawCurve->data()->constBegin()->value,100.);
    ResultRepository repo;QVERIFY(repo.open(settings.databasePath).isEmpty());QVERIFY(repo.list().records.isEmpty());
}
void UiTests::previewFailureDeletionAndGenerator()
{
    WorkflowDocument doc(makeScientificRegistry());const auto root=doc.addRoot("input_series").nodeId;
    const auto lowess=doc.addChild(root,ChildSlot::Down,"detrend_lowess").nodeId;
    const auto target=doc.addChild(lowess,ChildSlot::Down,"affine").nodeId;
    SourceItem invalid{"unordered",makeData(SeriesData{{0,2,1},{1,3,2},{}, {}}),{}};
    WorkflowPreviewController c(&doc);c.setSource(root,invalid,true);c.selectNode(target);c.setActive(true);
    QTRY_COMPARE(c.state(),WorkflowPreviewController::State::Failed);
    QCOMPARE(c.result()->failedNode,lowess);QVERIFY(!c.result()->executed.contains(target));
    QVERIFY(!c.result()->output.error.isEmpty());
    doc.removeSubtree(lowess);QCOMPARE(c.state(),WorkflowPreviewController::State::Idle);QVERIFY(!c.result());
    const auto generated=doc.addRoot("sine_source",{{"count",8}}).nodeId;
    c.setSource({}, {}, false);c.selectNode(generated);QTRY_COMPARE(c.state(),WorkflowPreviewController::State::Ready);
    QCOMPARE(c.result()->executed,QVector<NodeId>({generated}));
    c.setActive(false);QCOMPARE(c.state(),WorkflowPreviewController::State::Paused);QVERIFY(!c.result());
}
void UiTests::previewMultipleOutputsAndDisplayBudget()
{
    WorkflowDocument doc(makeScientificRegistry());const auto root=doc.addRoot("sine_source",{{"count",128}}).nodeId;
    const auto mtm=doc.addChild(root,ChildSlot::Down,"mtm").nodeId;
    WorkflowPreviewPanel panel(&doc);panel.resize(440,850);panel.show();panel.controller()->selectNode(mtm);
    QTRY_COMPARE_WITH_TIMEOUT(panel.controller()->state(),WorkflowPreviewController::State::Ready,10000);
    auto *outputs=panel.findChild<QComboBox*>("previewOutput");auto *plot=panel.findChild<PlotView*>("nodePreviewPlot");
    QVERIFY(outputs&&plot);QCOMPARE(outputs->currentData().toString(),QString("spectrum"));QVERIFY(plot->plottedCount()>1);
    const auto first=panel.controller()->result();QSignalSpy started(panel.controller(),&WorkflowPreviewController::started);
    outputs->setCurrentIndex(outputs->findData("peaks"));QVERIFY(dataAs<TableData>(plot->currentData()));
    QCOMPARE(panel.controller()->result(),first);QCOMPARE(started.count(),0);
    SeriesData large;for(int i=0;i<100000;++i){large.x<<i;large.y<<(i==12345?1000.:1.);}
    auto full=NodeResult::success({{"series",makeData(large)}});const auto display=reducePreviewData(full,{});
    const auto *small=dataAs<SeriesData>(display.outputs["series"]);QVERIFY(small);QVERIFY(small->x.size()<=4096);QVERIFY(small->y.contains(1000.));
    QCOMPARE(dataAs<SeriesData>(full.outputs["series"])->x.size(),100000);QVERIFY(display.report["display_reduced"].toBool());
    TableData table;for(int i=0;i<300;++i)table.columns<<QString::number(i);
    for(int i=0;i<2048;++i)table.rows<<QVector<double>(300,i);
    const auto tables=reducePreviewData(NodeResult::success({{"table",makeData(table)}}),{});
    const auto *t=dataAs<TableData>(tables.outputs["table"]);QVERIFY(t);
    QVERIFY(t->columns.size()<=PreviewLimits::tableColumns);
    QVERIFY(t->columns.size()*t->rows.size()<=PreviewLimits::tableCells);
    GridData grid;grid.rows=2;grid.columns=500;grid.x.resize(500);grid.y={1,2};grid.values.resize(1000);
    for(int i=0;i<500;++i){grid.x[i]=i;grid.values[i]=i;grid.values[500+i]=i+1;}
    const auto grids=reducePreviewData(NodeResult::success({{"power",makeData(grid)},{"significance_ratio",makeData(grid)}}),{});
    const auto *g=dataAs<GridData>(grids.outputs["power"]),*ratio=dataAs<GridData>(grids.outputs["significance_ratio"]);
    QVERIFY(g&&ratio);QCOMPARE(g->x,ratio->x);QCOMPARE(g->y,ratio->y);QCOMPARE(g->columns,384);
}
void UiTests::previewCloseAndDesktopCapture()
{
    QTemporaryDir dir;AppSettings settings;settings.databasePath=dir.filePath("local.sqlite");
    MainWindow window(settings,false);window.resize(1640,960);window.show();
    const auto path=dir.filePath("Climate_record.csv");
    QFile input(path);QVERIFY(input.open(QIODevice::WriteOnly));input.write("Age,Signal\n");
    for(int i=0;i<128;++i)input.write(QByteArray::number(1000+i)+','+QByteArray::number(.02*i+std::sin(i*.25))+'\n');
    input.close();auto *sources=window.findChild<SourcePanel*>();sources->addFiles({path});
    QTRY_VERIFY(sources->readyToRun());const auto root=sources->root();
    const auto child=window.document()->addChild(root,ChildSlot::Down,"linear_detrend").nodeId;
    window.canvas()->nodeSelected(child);
    auto *preview=window.findChild<WorkflowPreviewPanel*>();
    QTRY_COMPARE(preview->controller()->state(),WorkflowPreviewController::State::Ready);
    auto *column=window.findChild<CollapsiblePanel*>("previewSidePanel");QVERIFY(!column->isCollapsed());
    QVERIFY(column->width() >= window.width() * .30 && column->width() <= window.width() * .38);
    auto *rawPlot = preview->findChild<QCustomPlot *>("filePreviewPlot");
    auto *outputPlot = preview->findChild<PlotView *>("nodePreviewPlot");
    QVERIFY(rawPlot && outputPlot);
    for (const auto *plot : {rawPlot, outputPlot->plotWidget()}) {
        const auto axes = plot->axisRect()->rect();
        QVERIFY(axes.width() > axes.height());
    }
    const int initialWidth = column->width();
    window.resize(1800, 960);
    QTRY_VERIFY(column->width() > initialWidth);
    window.resize(1640, 960);
    QTRY_VERIFY(qAbs(column->width() - initialWidth) <= 1);
    const auto capture=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if(!capture.isEmpty()) {
        QDir().mkpath(capture);QTest::qWait(60);QVERIFY(window.grab().save(capture+"/workflow_preview.png"));
        ThemeManager::instance().apply(*qApp,"dark");QTest::qWait(60);QVERIFY(window.grab().save(capture+"/workflow_preview_dark.png"));
        ThemeManager::instance().apply(*qApp,"light");window.resize(1366,768);QTest::qWait(60);
        QVERIFY(window.grab().save(capture+"/workflow_preview_1366.png"));
    }
    const int openWidth = column->width();
    column->setCollapsed(true);QCOMPARE(preview->controller()->state(),WorkflowPreviewController::State::Paused);
    column->setCollapsed(false);QTRY_COMPARE(preview->controller()->state(),WorkflowPreviewController::State::Ready);
    QVERIFY(qAbs(column->width() - openWidth) <= 1);
    window.close();QTRY_VERIFY(!window.isVisible());
}

void UiTests::previewFileErrorsAndInputReplacement()
{
    QTemporaryDir dir; const auto a=dir.filePath("a.csv"), b=dir.filePath("b.csv");
    for(const auto &path:{a,b}) {QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(path==a ? "x,y\n0,1\n1,2\n2,3\n" : "x,y\n0,10\n1,20\n2,30\n");}
    WorkflowDocument doc(makeScientificRegistry());const auto root=doc.addRoot("file_source").nodeId;
    const auto child=doc.addChild(root,ChildSlot::Down,"affine",{{"factor",2}}).nodeId;
    SeriesFileOptions options;options.header=true;options.autoHeader=false;
    auto input=[&](QString path){return SourceItem{path,{},{{"path",path},{"file_options",fileOptionsToJson(options)}}};};
    WorkflowPreviewController c(&doc);c.setActive(true);c.selectNode(child);c.setSource(root,input(a),true);
    QTRY_COMPARE(c.state(),WorkflowPreviewController::State::Ready);
    options.header=false;c.setSource(root,input(a),true);QVERIFY(!c.result());
    QTRY_COMPARE(c.state(),WorkflowPreviewController::State::Failed);QCOMPARE(c.result()->failedNode,root);
    QVERIFY(!c.result()->executed.contains(child));
    options.skipInvalidRows=true;c.setSource(root,input(a),true);
    QTRY_COMPARE(c.state(),WorkflowPreviewController::State::Ready);
    QCOMPARE(dataAs<SeriesData>(c.result()->output.outputs["series"])->y,QVector<double>({2,4,6}));
    QSignalSpy started(&c,&WorkflowPreviewController::started);
    c.setSource(root,input(b),true);c.setSource(root,input(a),true);c.setSource(root,input(b),true);
    QTRY_COMPARE(c.state(),WorkflowPreviewController::State::Ready);QCOMPARE(started.count(),1);
    QCOMPARE(dataAs<SeriesData>(c.result()->output.outputs["series"])->y,QVector<double>({20,40,60}));
    c.setSource(root,{},false);QCOMPARE(c.state(),WorkflowPreviewController::State::Idle);QVERIFY(!c.result());
}

// Recovery integration tests exercise the restored public UI and the real registry.
void UiTests::restoredCatalogueMatchesMenusAndDrag()
{
    const auto registry = makeScientificRegistry();
    for (const auto& language : {"zh", "en"}) {
        setLanguage(language);
        TaskCatalog catalog(registry);
        auto* tree = catalog.findChild<QTreeWidget*>("taskCatalog"); QVERIFY(tree);
        QString picked;
        auto* menu = catalog.menu(&catalog, [](const TaskDescriptor&){ return true; },
                                  [&](QString key){ picked = key; });
        QMap<QString, QStringList> treePaths;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            auto* item = *it;
            const auto key = item->data(0, Qt::UserRole).toString();
            if (key.isEmpty()) continue;
            QStringList path;
            for (auto* p = item->parent(); p; p = p->parent()) path.prepend(p->text(0));
            QVERIFY(!treePaths.contains(key)); treePaths[key] = path;
        }
        int visible = 0;
        for (const auto& d : registry.descriptors()) {
            if (!d.catalogVisible) { QVERIFY(!findAction(menu, d.key)); continue; }
            ++visible;
            QStringList expected; for (const auto& c : d.category) expected << categoryLabel(c);
            QCOMPARE(treePaths.value(d.key), expected);
            auto* action = findAction(menu, d.key); QVERIFY(action);
            QCOMPARE(action->text(), methodLabel(d));
            QStringList actual;
            for (auto* p = qobject_cast<QMenu*>(action->parent()); p && p != menu;
                 p = qobject_cast<QMenu*>(p->parent())) actual.prepend(p->title());
            QCOMPARE(actual, expected);
            action->trigger(); QCOMPARE(picked, d.key);
        }
        QCOMPARE(treePaths.size(), visible);
        QSignalSpy activated(&catalog, &TaskCatalog::methodActivated);
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toString() == "evolution_fft")
                tree->itemDoubleClicked(*it, 0);
        QCOMPARE(activated.count(), 1); QCOMPARE(activated.front()[0].toString(), QString("evolution_fft"));
        delete menu;
    }
    setLanguage("zh");
    WorkflowDocument doc(registry);
    const auto root = doc.addRoot("sine_source").nodeId;
    WorkflowCanvas canvas(&doc); canvas.resize(900,650); canvas.show();
    canvas.setSelectedNode(root); canvas.fitWorkflow();
    QSignalSpy dropped(&canvas, &WorkflowCanvas::methodDropped);
    QMimeData mime; mime.setData(TaskMimeType, "evolution_mtm");
    const auto pos = canvas.addViewportPosition(root);
    QDragEnterEvent enter(pos,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(),&enter); QVERIFY(enter.isAccepted());
    QDropEvent drop(pos,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(),&drop); QCOMPARE(dropped.count(),1);
    QCOMPARE(dropped.front()[0].toString(),QString("evolution_mtm"));
    QCOMPARE(dropped.front()[1].toUuid(),root);
    QVERIFY(doc.addChild(root,dropped.front()[2].value<ChildSlot>(),"evolution_mtm").ok());
}

void UiTests::restoredFilePickerCommitsAndCancels()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("precession_tilt_source").nodeId;
    ParameterEditor editor; editor.resize(320,750); editor.showNode(doc.node(root),doc.model().registry()); editor.show();
    auto* edit = editor.findChild<QLineEdit*>("parameter_pt_table_path");
    auto* browse = editor.findChild<QToolButton*>("parameterBrowse_pt_table_path");
    QVERIFY(edit && browse); QVERIFY(browse->isVisible()); QVERIFY(edit->text().isEmpty());
    const auto capture=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if(!capture.isEmpty()) { QDir().mkpath(capture); QTest::qWait(60);
        QVERIFY(editor.grab().save(capture+"/orbital_file_parameter.png")); }
    QSignalSpy committed(&editor,&ParameterEditor::applyRequested);
    connect(&editor,&ParameterEditor::applyRequested,&doc,[&]{
        QVERIFY(doc.setParameters(root,editor.parameters()).ok()); editor.markClean();
    });
    QTemporaryDir dir; const auto path = dir.filePath("PT.De1.0000Td0.4000.dat");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("test picker only\n"); file.close();
    const bool wasNativeDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
    bool seen = false, accept = true;
    int polls = 0;
    QTimer driver; driver.setInterval(50);
    connect(&driver,&QTimer::timeout,&editor,[&]{
        ++polls;
        for(auto* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QFileDialog*>(widget);
            if(!dialog || !dialog->isVisible()) continue;
            seen = true;
            if(accept && polls < 100) {
                // Type into Qt's non-native chooser, rather than racing the
                // asynchronous filesystem model's selectFile implementation.
                if(auto* name = dialog->findChild<QLineEdit*>("fileNameEdit")) name->setText(path);
                QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
            } else dialog->reject();
        }
    });
    driver.start(); QTest::mouseClick(browse,Qt::LeftButton); driver.stop(); QVERIFY(seen);
    QCOMPARE(edit->text(),path); QTRY_COMPARE(committed.count(),1);
    QCOMPARE(doc.node(root)->parameters["pt_table_path"].toString(),path);
    accept = false; seen = false; polls = 0; driver.start(); QTest::mouseClick(browse,Qt::LeftButton); driver.stop();
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,wasNativeDisabled);
    QVERIFY(seen); QCOMPARE(edit->text(),path); QVERIFY(!editor.dirty());
    QTest::qWait(300); QCOMPARE(committed.count(),1);
    edit->clear(); QTRY_COMPARE(committed.count(),2);
    QCOMPARE(doc.node(root)->parameters["pt_table_path"].toString(), QString());
}

void UiTests::restoredPreviewAndExport_data()
{
    QTest::addColumn<QString>("method");
    for (const auto *method :
         {"gaussian_filter", "taner_filter", "hilbert_envelope", "evolution_fft", "evolution_mtm",
          "select_interval", "demean", "standardize", "sampling_check", "moving_average",
          "savitzky_golay", "fft_spectrum", "welch_spectrum", "lomb_scargle", "butterworth_lowpass",
          "butterworth_highpass"})
        QTest::newRow(method) << QString(method);
}
void UiTests::restoredPreviewAndExport()
{
    QFETCH(QString, method);
    WorkflowDocument doc(makeScientificRegistry());
    const auto root =
        doc.addRoot("sine_source", {{"count", 256}, {"step", 1}, {"period", 24}, {"slope", 0}})
            .nodeId;
    const auto added = doc.addChild(root, ChildSlot::Down, method);
    QVERIFY(added.ok());
    const auto target = added.nodeId;
    WorkflowPreviewPanel panel(&doc);
    panel.resize(440, 850);
    panel.show();
    auto *controller = panel.controller();
    controller->selectNode(target);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(), WorkflowPreviewController::State::Ready, 15000);
    auto *outputs = panel.findChild<QComboBox *>("previewOutput");
    auto *plot = panel.findChild<PlotView *>("nodePreviewPlot");
    QVERIFY(outputs && plot);
    const auto request = prepareNodePreview(doc.model(), target, {}, {});
    QVERIFY(request.validation.ok());
    const auto run = WorkflowExecutor().run(request.requests.front());
    QCOMPARE(run.state, RunState::Succeeded);
    const auto expected = run.nodes[target];
    const auto preview = controller->result();
    QVERIFY(preview);
    const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!capture.isEmpty())
    {
        QDir().mkpath(capture);
        QTest::qWait(60);
        QVERIFY(panel.grab().save(capture + "/preview_" + method + ".png"));
        if (QStringList{"moving_average", "savitzky_golay", "fft_spectrum", "welch_spectrum",
                        "lomb_scargle", "butterworth_lowpass", "butterworth_highpass"}
                .contains(method))
        {
            ParameterEditor editor;
            editor.resize(320, 750);
            editor.showNode(doc.node(target), doc.model().registry());
            editor.show();
            QTest::qWait(30);
            QVERIFY(editor.grab().save(capture + "/parameters_" + method + ".png"));
        }
    }
    QCOMPARE(outputs->count(), expected.outputs.size());
    QSignalSpy restarted(controller, &WorkflowPreviewController::started);
    QTemporaryDir directory;
    for (auto it = expected.outputs.begin(); it != expected.outputs.end(); ++it)
    {
        const auto actual = preview->output.outputs.value(it.key());
        QVERIFY(actual);
        const auto a = encodeData(actual), b = encodeData(it.value());
        QVERIFY(a.ok() && b.ok());
        QCOMPARE(a.bytes, b.bytes);
        const int index = outputs->findData(it.key());
        QVERIFY(index >= 0);
        outputs->setCurrentIndex(index);
        QVERIFY(plot->hasOutput());
        QCOMPARE(plot->currentPort(), it.key());
        if (dataKind(*actual) == DataKind::Series || dataKind(*actual) == DataKind::Spectrum)
        {
            const bool pairedEnvelope = QStringList{"gaussian_filter", "taner_filter", "hilbert_envelope"}.contains(method) &&
                QStringList{"envelope", "filtered", "signal"}.contains(it.key());
            QCOMPARE(plot->plottedCount(), pairedEnvelope ? 2 : 1);
            // Quadrature and frequency-response ports remain independent.
        }
        if (dataKind(*actual) == DataKind::Grid)
        {
            QVERIFY(!plot->hasSignificanceGrid());
            QCOMPARE(plot->significanceSegmentCount(), 0);
            auto style = plot->style();
            style.colorLog = true;
            style.colorGradient = 2;
            QVERIFY(plot->applyStyle(style).isEmpty());
        }
        if (dataKind(*actual) != DataKind::Table)
        {
            const auto png = directory.filePath(it.key() + ".png");
            QVERIFY(plot->exportPng(png));
            QImage image(png);
            QVERIFY(!image.isNull());
            QVERIFY(image.width() > 100);
        }
    }
    QCOMPARE(restarted.count(), 0);
    QCOMPARE(controller->result(), preview);
    // setParameters replaces the complete parameter object. Preserve the source
    // count/step rather than silently reverting to the 128-point source default.
    auto parameters = doc.node(root)->parameters;
    parameters["amplitude"] = 3;
    QVERIFY(doc.setParameters(root, parameters).ok());
    QTRY_COMPARE_WITH_TIMEOUT(restarted.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(), WorkflowPreviewController::State::Ready, 15000);
    QVERIFY(controller->result() != preview);
    if (method == "lomb_scargle" || method.startsWith("butterworth_"))
    {
        auto invalid = doc.node(root)->parameters;
        if (method == "lomb_scargle")
            invalid["amplitude"] = 0;
        else
            invalid["count"] = 8;
        QVERIFY(doc.setParameters(root, invalid).ok());
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(), WorkflowPreviewController::State::Failed,
                                  15000);
        QVERIFY(controller->result());
        QVERIFY(!controller->result()->output.diagnostic().isEmpty());
        QVERIFY(doc.setParameters(root, parameters).ok());
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(), WorkflowPreviewController::State::Ready,
                                  15000);
    }
    if (method == "welch_spectrum")
    {
        // A genuine short-record edit must fail visibly, never silently shrink
        // the requested 256-point Welch window or keep showing the old result.
        parameters["count"] = 128;
        QVERIFY(doc.setParameters(root, parameters).ok());
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(), WorkflowPreviewController::State::Failed,
                                  15000);
        QVERIFY(controller->result());
        QVERIFY(!controller->result()->output.diagnostic().isEmpty());
        parameters["count"] = 256;
        QVERIFY(doc.setParameters(root, parameters).ok());
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(), WorkflowPreviewController::State::Ready,
                                  15000);
    }
}

void UiTests::cocoAnalysisPreview_data()
{
    QTest::addColumn<QString>("method");
    QTest::newRow("monte-carlo")<<QString("coco_monte_carlo");
    QTest::newRow("sliding-coco")<<QString("evolution_coco");
}
void UiTests::cocoAnalysisPreview()
{
    QFETCH(QString,method);setLanguage("zh");WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("input_series").nodeId;
    QJsonObject params{{"coco_rate_min",3},{"coco_rate_max",5},{"coco_rate_step",.25}};
    if(method=="coco_monte_carlo")params["coco_simulations"]=39;
    else{params["ecoco_window"]=512;params["ecoco_step"]=128;}
    const auto target=doc.addChild(root,ChildSlot::Down,method,params).nodeId;QVERIFY(!target.isNull());
    SeriesData data;data.xAxis={"Depth","m"};data.yAxis={"Proxy","u"};
    for(int i=0;i<1024;++i){data.x<<i*.1;double y=0;for(double p:{405.,125.,95.,41.,24.,22.,19.})y+=std::sin(2*3.14159265358979323846*i*2.5/p);data.y<<y;}
    SourceItem input{"COCO proxy",makeData(data),{}};doc.setSources(root,{input});
    WorkflowPreviewPanel panel(&doc);panel.resize(560,850);panel.show();auto *controller=panel.controller();controller->setSource(root,input,true);controller->selectNode(target);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,20000);
    auto request=prepareNodePreview(doc.model(),target,root,input);QVERIFY(request.validation.ok());
    const auto run=WorkflowExecutor().run(request.requests.front());QCOMPARE(run.state,RunState::Succeeded);const auto expected=run.nodes.value(target);
    auto *outputs=panel.findChild<QComboBox*>("previewOutput");auto *plot=panel.findChild<PlotView*>("nodePreviewPlot");QVERIFY(outputs&&plot);QCOMPARE(outputs->count(),expected.outputs.size());
    QSignalSpy started(controller,&WorkflowPreviewController::started);QTemporaryDir dir;
    for(auto it=expected.outputs.cbegin();it!=expected.outputs.cend();++it){QCOMPARE(encodeData(controller->result()->output.outputs[it.key()]).bytes,encodeData(it.value()).bytes);
        const auto index=outputs->findData(it.key());QVERIFY(index>=0);outputs->setCurrentIndex(index);QVERIFY(plot->hasOutput());QCOMPARE(plot->currentPort(),it.key());
        if(dataKind(*it.value())!=DataKind::Table)QVERIFY(plot->exportPng(dir.filePath(it.key()+".png")));}
    QCOMPARE(started.count(),0);auto previous=controller->result();controller->selectNode({});QTest::qWait(100);QCOMPARE(controller->result(),previous);QCOMPARE(started.count(),0);controller->selectNode(target);
    ParameterEditor editor;editor.resize(400,900);editor.showNode(doc.node(target),doc.model().registry());editor.show();
    connect(&editor,&ParameterEditor::applyRequested,&doc,[&]{QVERIFY(doc.setParameters(target,editor.parameters()).ok());editor.markClean();});
    if(method=="coco_monte_carlo"){
        auto *model=editor.findChild<QComboBox*>("parameter_coco_null");auto *rho=editor.findChild<QWidget*>("parameterRow_coco_rho");auto *peaks=editor.findChild<QWidget*>("parameterRow_coco_null_peaks");QVERIFY(model&&rho&&peaks);
        QVERIFY(!rho->isHidden());QVERIFY(peaks->isHidden());model->setCurrentIndex(model->findData("random_peaks"));QVERIFY(rho->isHidden());QVERIFY(!peaks->isHidden());
        QTRY_COMPARE_WITH_TIMEOUT(doc.node(target)->parameters["coco_null"].toString(),QString("random_peaks"),5000);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,20000);
        QVERIFY(controller->result()->output.warnings.contains("warning.coco_random_reference"));
    } else {
        auto *step=editor.findChild<QSpinBox*>("parameter_ecoco_step");QVERIFY(step);step->setValue(64);
        QTRY_COMPARE_WITH_TIMEOUT(doc.node(target)->parameters["ecoco_step"].toInt(),64,5000);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,20000);QCOMPARE(controller->result()->output.report["windows"].toInt(),9);
    }
    outputs->setCurrentIndex(outputs->findData(method=="coco_monte_carlo"?"scan_adjusted_p":"correlation_map"));
    bool hasMethodNotes=false;
    for(auto *widget:panel.findChildren<QWidget*>())if(widget->toolTip().contains(translatedLabel(method=="coco_monte_carlo"?"warning.coco_random_reference":"warning.ecoco_descriptive",QString())))hasMethodNotes=true;
    QVERIFY(hasMethodNotes);
    const auto capture=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");if(!capture.isEmpty()){QDir().mkpath(capture);QTest::qWait(50);QVERIFY(panel.grab().save(capture+"/preview_"+method+".png"));QVERIFY(editor.grab().save(capture+"/parameters_"+method+".png"));}
    auto invalid=input;auto bad=data;bad.y.fill(1);invalid.data=makeData(bad);controller->setSource(root,invalid,true);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Failed,20000);
    QVERIFY(!controller->result()->output.diagnostic().isEmpty());controller->setSource(root,input,true);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,20000);
}

void UiTests::cocoPreview()
{
    setLanguage("zh");
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("input_series").nodeId;
    const auto target=doc.addChild(root,ChildSlot::Down,"coco_scan",{{"coco_rate_min",3},{"coco_rate_max",5},{"coco_rate_step",.25}}).nodeId;
    QVERIFY(!target.isNull());
    SeriesData data;data.xAxis={"Depth","m"};data.yAxis={"Proxy","u"};
    for(int i=0;i<1024;++i){data.x<<i*.1;double y=0;for(double p:{405.,125.,95.,41.,24.,22.,19.})y+=std::sin(2*3.14159265358979323846*i*2.5/p);data.y<<y;}
    SourceItem input{"COCO proxy",makeData(data),{}};doc.setSources(root,{input});
    WorkflowPreviewPanel panel(&doc);panel.resize(560,850);panel.show();auto *controller=panel.controller();controller->setSource(root,input,true);controller->selectNode(target);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
    auto request=prepareNodePreview(doc.model(),target,root,input);QVERIFY(request.validation.ok());const auto run=WorkflowExecutor().run(request.requests.front());QCOMPARE(run.state,RunState::Succeeded);const auto expected=run.nodes.value(target);
    auto *outputs=panel.findChild<QComboBox*>("previewOutput");auto *plot=panel.findChild<PlotView*>("nodePreviewPlot");QVERIFY(outputs&&plot);QCOMPARE(outputs->count(),5);
    QSignalSpy started(controller,&WorkflowPreviewController::started);QTemporaryDir dir;
    for(auto it=expected.outputs.cbegin();it!=expected.outputs.cend();++it){QCOMPARE(encodeData(controller->result()->output.outputs[it.key()]).bytes,encodeData(it.value()).bytes);int index=outputs->findData(it.key());QVERIFY(index>=0);outputs->setCurrentIndex(index);QVERIFY(plot->hasOutput());QCOMPARE(plot->currentPort(),it.key());if(dataKind(*it.value())==DataKind::Series)QVERIFY(plot->exportPng(dir.filePath(it.key()+".png")));}
    QCOMPARE(started.count(),0);auto previous=controller->result();controller->selectNode({});QTest::qWait(100);QCOMPARE(controller->result(),previous);QCOMPARE(started.count(),0);controller->selectNode(target);
    ParameterEditor editor;editor.resize(380,900);editor.showNode(doc.node(target),doc.model().registry());editor.show();connect(&editor,&ParameterEditor::applyRequested,&doc,[&]{QVERIFY(doc.setParameters(target,editor.parameters()).ok());editor.markClean();});
    auto *step=editor.findChild<QDoubleSpinBox*>("parameter_coco_rate_step");QVERIFY(step);step->setValue(.5);QTRY_VERIFY_WITH_TIMEOUT(started.count()>0,5000);QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);QCOMPARE(controller->result()->output.report["rate_step"].toDouble(),.5);
    outputs->setCurrentIndex(outputs->findData("correlation"));
    const auto capture=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");if(!capture.isEmpty()){QDir().mkpath(capture);QTest::qWait(40);QVERIFY(panel.grab().save(capture+"/preview_coco.png"));QVERIFY(editor.grab().save(capture+"/parameters_coco.png"));}
    auto invalid=input;auto bad=data;bad.y.fill(1);invalid.data=makeData(bad);controller->setSource(root,invalid,true);QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Failed,15000);QCOMPARE(controller->result()->output.diagnostic().code,QString("COCO_NO_SCORE"));controller->setSource(root,input,true);QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
    auto *mode=editor.findChild<QComboBox*>("parameter_coco_target");auto *periods=editor.findChild<QWidget*>("parameterRow_coco_periods");auto *age=editor.findChild<QWidget*>("parameterRow_target_age_ma");QVERIFY(mode&&periods&&age);QVERIFY(periods->isVisible());QVERIFY(!age->isVisible());mode->setCurrentIndex(mode->findData("waltham_midpoint"));QVERIFY(!periods->isVisible());QVERIFY(age->isVisible());QTRY_COMPARE_WITH_TIMEOUT(doc.node(target)->parameters["coco_target"].toString(),QString("waltham_midpoint"),5000);QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
}

void UiTests::cycleTuningPreview_data()
{
    QTest::addColumn<QString>("method");
    QTest::newRow("cycle-markers") << QString("cycle_age_model");
    QTest::newRow("target-calculator") << QString("milankovitch_period");
}
void UiTests::cycleTuningPreview()
{
    QFETCH(QString, method);
    setLanguage("zh");
    WorkflowDocument doc(makeScientificRegistry());
    const bool sourceTask=method=="milankovitch_period";
    const auto root=doc.addRoot(sourceTask?method:"input_series").nodeId;
    const auto target=sourceTask?root:doc.addChild(root,ChildSlot::Down,method).nodeId;
    QVERIFY(!target.isNull());
    SeriesData data{{0,1,2,3,4,5,6,7,8,9,10},{0,1,0,-1,0,1,0,-1,0,1,0},{"Depth","m"},{"Proxy","u"}};
    SourceItem input{"Cycle proxy",makeData(data),{}};
    if(!sourceTask)doc.setSources(root,{input});
    WorkflowPreviewPanel panel(&doc);panel.resize(540,850);panel.show();
    auto *controller=panel.controller();
    if(!sourceTask)controller->setSource(root,input,true);
    controller->selectNode(target);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
    auto request=prepareNodePreview(doc.model(),target,sourceTask?NodeId{}:root,sourceTask?SourceItem{}:input);
    QVERIFY2(request.validation.ok(),qPrintable(request.validation.message()));
    const auto run=WorkflowExecutor().run(request.requests.front());QCOMPARE(run.state,RunState::Succeeded);
    const auto expected=run.nodes.value(target);
    auto *outputs=panel.findChild<QComboBox*>("previewOutput");auto *plot=panel.findChild<PlotView*>("nodePreviewPlot");
    QVERIFY(outputs && plot);QCOMPARE(outputs->count(),expected.outputs.size());
    QSignalSpy started(controller,&WorkflowPreviewController::started);QTemporaryDir directory;
    for(auto it=expected.outputs.cbegin();it!=expected.outputs.cend();++it){
        QCOMPARE(encodeData(controller->result()->output.outputs[it.key()]).bytes,encodeData(it.value()).bytes);
        const auto index=outputs->findData(it.key());QVERIFY(index>=0);outputs->setCurrentIndex(index);
        QVERIFY(plot->hasOutput());QCOMPARE(plot->currentPort(),it.key());
        if(dataKind(*it.value())==DataKind::Series)QVERIFY(plot->exportPng(directory.filePath(it.key()+".png")));
    }
    QCOMPARE(started.count(),0);const auto previous=controller->result();
    controller->selectNode({});QTest::qWait(100);QCOMPARE(controller->result(),previous);QCOMPARE(started.count(),0);
    controller->selectNode(target);
    outputs->setCurrentIndex(outputs->findData(sourceTask?"target_period":"age_model"));
    ParameterEditor editor;editor.resize(340,850);editor.showNode(doc.node(target),doc.model().registry());editor.show();
    connect(&editor,&ParameterEditor::applyRequested,&doc,[&]{QVERIFY(doc.setParameters(target,editor.parameters()).ok());editor.markClean();});
    auto *spin=editor.findChild<QDoubleSpinBox*>(sourceTask?"parameter_target_age_ma":"parameter_cycle_period");QVERIFY(spin);
    spin->setValue(sourceTask?600:23);
    QTRY_VERIFY_WITH_TIMEOUT(started.count()>0,5000);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
    if(sourceTask){const auto *t=dataAs<TableData>(controller->result()->output.outputs["target_period"]);QVERIFY(t);QCOMPARE(t->rows[0][0],600.);}
    else{
        const auto *t=dataAs<SeriesData>(controller->result()->output.outputs["age_model"]);QVERIFY(t);QCOMPARE(t->y.front(),23.);
        auto *mode=editor.findChild<QComboBox*>("parameter_cycle_period_source");QVERIFY(mode);
        auto *periodRow=editor.findChild<QWidget*>("parameterRow_cycle_period");auto *ageRow=editor.findChild<QWidget*>("parameterRow_target_age_ma");QVERIFY(periodRow&&ageRow);
        QVERIFY(!ageRow->isVisible());mode->setCurrentIndex(mode->findData("model_midpoint"));QVERIFY(!periodRow->isVisible());QVERIFY(ageRow->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(doc.node(target)->parameters["cycle_period_source"].toString(),QString("model_midpoint"),5000);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
    }
    const auto capture=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if(!capture.isEmpty()){QDir().mkpath(capture);QTest::qWait(40);QVERIFY(panel.grab().save(capture+"/preview_"+method+".png"));QVERIFY(editor.grab().save(capture+"/parameters_"+method+".png"));}
    if(!sourceTask){auto bad=input;data.y.fill(1);bad.data=makeData(data);controller->setSource(root,bad,true);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Failed,15000);
        QCOMPARE(controller->result()->output.diagnostic().code,QString("CYCLE_ANCHORS"));controller->setSource(root,input,true);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
    }
}

void UiTests::chronologyPreview_data()
{
    QTest::addColumn<QString>("method");
    QTest::newRow("rate") << QString("sedrate_to_age");
    QTest::newRow("controls") << QString("apply_age_model");
}
void UiTests::chronologyPreview()
{
    QFETCH(QString, method);
    QTemporaryDir directory;
    const auto path=directory.filePath("control.csv");
    auto writeModel=[&](const QByteArray& bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    QVERIFY(writeModel("depth,age\n0,10\n1,12\n2,16\n"));
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("input_series").nodeId;
    QJsonObject parameters;
    if(method=="apply_age_model") parameters["chrono_model_file"]=path;
    const auto added=doc.addChild(root,ChildSlot::Down,method,parameters);
    QVERIFY(added.ok()); const auto target=added.nodeId;
    SeriesData data{{0,.5,1,2},{2,4,1,3},{"Depth","m"},{"Rate","cm/ka"}};
    if(method=="apply_age_model")data.yAxis={"Proxy","u"};
    SourceItem input{"Test input",makeData(data),{}};
    doc.setSources(root,{input});
    WorkflowPreviewPanel panel(&doc);panel.resize(440,850);panel.show();
    auto *controller=panel.controller();controller->setSource(root,input,true);controller->selectNode(target);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
    const auto request=prepareNodePreview(doc.model(),target,root,input);QVERIFY(request.validation.ok());
    const auto run=WorkflowExecutor().run(request.requests.front());QCOMPARE(run.state,RunState::Succeeded);
    auto preview=controller->result();QVERIFY(preview);
    auto *outputs=panel.findChild<QComboBox*>("previewOutput");auto *plot=panel.findChild<PlotView*>("nodePreviewPlot");
    QVERIFY(outputs && plot);QCOMPARE(outputs->count(),run.nodes[target].outputs.size());
    QSignalSpy started(controller,&WorkflowPreviewController::started);
    const auto expected=run.nodes.value(target);
    for(auto it=expected.outputs.cbegin();it!=expected.outputs.cend();++it){
        QCOMPARE(encodeData(preview->output.outputs[it.key()]).bytes,encodeData(it.value()).bytes);
        const auto index=outputs->findData(it.key());QVERIFY(index>=0);outputs->setCurrentIndex(index);
        QVERIFY(plot->hasOutput());QCOMPARE(plot->currentPort(),it.key());
        if(dataKind(*it.value())==DataKind::Series){QCOMPARE(plot->plottedCount(),1);QVERIFY(plot->exportPng(directory.filePath(it.key()+".png")));}
    }
    QCOMPARE(started.count(),0);
    controller->selectNode({});QTest::qWait(100);QCOMPARE(controller->result(),preview);QCOMPARE(started.count(),0);
    outputs->setCurrentIndex(outputs->findData(method=="apply_age_model"?"aged_series":"age_model"));
    auto capture=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if(!capture.isEmpty()){
        QTest::qWait(40);QVERIFY(panel.grab().save(capture+"/preview_"+method+".png"));
        ParameterEditor editor;editor.resize(320,750);editor.showNode(doc.node(target),doc.model().registry());editor.show();QTest::qWait(40);
        QVERIFY(editor.grab().save(capture+"/parameters_"+method+".png"));
    }
    // Editing the external file is explicit: Recalculate rereads the file.
    if(method=="apply_age_model"){
        QVERIFY(writeModel("0,20\n1,22\n2,26\n"));controller->retry();
        QTRY_COMPARE_WITH_TIMEOUT(started.count(),1,5000);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
        const auto *series=dataAs<SeriesData>(controller->result()->output.outputs["aged_series"]);QVERIFY(series);QCOMPARE(series->x.front(),20.);
        QVERIFY(QFile::remove(path));controller->retry();
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Failed,15000);
        QVERIFY(!controller->result()->output.diagnostic().isEmpty());
        QVERIFY(writeModel("0,10\n1,12\n2,16\n"));controller->retry();
    } else {
        auto p=doc.node(target)->parameters;p["chrono_first_age"]=10;QVERIFY(doc.setParameters(target,p).ok());
        QTRY_COMPARE_WITH_TIMEOUT(started.count(),1,5000);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
        const auto *series=dataAs<SeriesData>(controller->result()->output.outputs["age_model"]);QVERIFY(series);QCOMPARE(series->y.front(),10.);
        data.y[2]=0;auto invalid=input;invalid.data=makeData(data);controller->setSource(root,invalid,true);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Failed,15000);
        QVERIFY(!controller->result()->output.diagnostic().isEmpty());controller->setSource(root,input,true);
    }
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
}

void UiTests::restoredOrbitalSourcesPreview()
{
    const auto registry = makeScientificRegistry();
    for (const auto *method : {"insolation_map", "eccentricity_source", "precession_tilt_source"})
    {
        WorkflowDocument doc(registry);
        const QJsonObject p = QString(method) == "insolation_map"
                                  ? QJsonObject{{"age_end_ka", 10}}
                                  : QJsonObject{{"orbit_end_ka", 10}};
        const auto root = doc.addRoot(method, p).nodeId;
        QVERIFY(!root.isNull());
        WorkflowPreviewPanel panel(&doc);
        panel.resize(440, 850);
        panel.show();
        panel.controller()->selectNode(root);
        QTRY_COMPARE_WITH_TIMEOUT(panel.controller()->state(),
                                  WorkflowPreviewController::State::Ready, 30000);
        auto *selector = panel.findChild<QComboBox *>("previewOutput");
        auto *plot = panel.findChild<PlotView *>("nodePreviewPlot");
        auto *source = generatedSourcePlot(panel);
        QVERIFY(selector && plot && source);
        const auto expected = registry.create(method, p)->execute({}, {});
        QCOMPARE(expected.state, NodeState::Succeeded);
        for(auto it=expected.outputs.begin();it!=expected.outputs.end();++it) {
            const int index=selector->findData(it.key()); QVERIFY(index>=0); selector->setCurrentIndex(index);
            const auto a=encodeData(plot->currentData()), b=encodeData(it.value());
            QVERIFY(a.ok() && b.ok()); QCOMPARE(a.bytes,b.bytes);
            QCOMPARE(source->currentPort(), it.key());
            QCOMPARE(encodeData(source->currentData()).bytes, a.bytes);
            if(dataKind(*it.value())==DataKind::Grid) { QVERIFY(!plot->hasSignificanceGrid()); QCOMPARE(plot->significanceSegmentCount(),0); }
        }
        QTemporaryDir dir;
        QVERIFY(plot->exportPng(dir.filePath("source.png")));
    }
}

void UiTests::catalogSingleClickAndTaskDoubleClick()
{
    TaskCatalog catalog(makeScientificRegistry());
    catalog.resize(340, 800);
    catalog.show();
    auto *tree = catalog.findChild<QTreeWidget *>("taskCatalog");
    QVERIFY(tree);
    QSignalSpy activated(&catalog, &TaskCatalog::methodActivated);
    auto *category = tree->topLevelItem(0);
    QVERIFY(category);
    QVERIFY(category->isExpanded());
    auto pos = tree->visualItemRect(category).center();
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QVERIFY(!category->isExpanded());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QVERIFY(category->isExpanded());
    const auto rect = tree->visualItemRect(category);
    const auto arrow =
        QPoint(std::max(2, rect.left() - tree->indentation() / 2), rect.center().y());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
    QVERIFY(!category->isExpanded());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, arrow);
    QVERIFY(category->isExpanded());
    QCOMPARE(activated.count(), 0);
    tree->expandAll();
    QTreeWidgetItem *task = nullptr;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole) == "moving_average")
            task = *it;
    QVERIFY(task);
    tree->scrollToItem(task);
    QTest::qWait(20);
    pos = tree->visualItemRect(task).center();
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QCOMPARE(activated.count(), 0);
    QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated[0][0].toString(), QString("moving_average"));
    auto *search = catalog.findChild<QLineEdit *>();
    search->setText("Welch");
    QTreeWidgetItem *welch = nullptr;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole) == "welch_spectrum")
            welch = *it;
    QVERIFY(welch && !welch->isHidden());
    QVERIFY(task->isHidden());
    search->clear();
}
void UiTests::dragCardThemeScaleAndSourceDrop()
{
    const auto registry = makeScientificRegistry();
    for (const auto *theme : {"light", "dark"})
    {
        ThemeManager::instance().apply(*qApp, theme);
        for (qreal dpr : {1., 1.5, 2.})
            for (const auto *key : {"sine_source", "savitzky_golay"})
            {
                const auto preview =
                    taskDragPreview(registry.find(key)->descriptor, qApp->font(), dpr);
                QVERIFY(!preview.pixmap.isNull());
                QCOMPARE(preview.pixmap.devicePixelRatio(), dpr);
                QCOMPARE(preview.hotSpot, QPoint(12, 12));
                const auto img = preview.pixmap.toImage();
                QVERIFY(img.pixelColor(0, 0).alpha() == 0);
                const auto bg = img.pixelColor(int(290 * dpr), int(20 * dpr));
                QVERIFY(bg.alpha() > 100 && bg.alpha() < 255);
                if (qEnvironmentVariableIsSet("ASTER_UI_CAPTURE_DIR") && dpr == 2. &&
                    QString(key) == "savitzky_golay")
                    QVERIFY(preview.pixmap.save(qEnvironmentVariable("ASTER_UI_CAPTURE_DIR") +
                                                "/drag_" + theme + ".png"));
            }
    }
    ThemeManager::instance().apply(*qApp, "light");
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("test.sqlite");
    MainWindow window(settings, false);
    window.resize(1500, 900);
    window.show();
    auto *canvas = window.canvas();
    QSignalSpy added(canvas, &WorkflowCanvas::sourceDropped);
    QMimeData mime;
    mime.setData(TaskMimeType, "sine_source");
    // A blank scene position, away from the start title/card.
    const QPoint pos = canvas->mapFromScene(QPointF(-80, -10));
    QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &drop);
    QVERIFY(drop.isAccepted());
    QCOMPARE(added.count(), 1);
    QCOMPARE(window.document()->model().definition().nodes.size(), qsizetype(1));
    QTRY_COMPARE(window.findChild<WorkflowPreviewPanel *>()->controller()->state(),
                 WorkflowPreviewController::State::Ready);
    canvas->setEditable(false);
    QDropEvent blocked(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas->viewport(), &blocked);
    QVERIFY(!blocked.isAccepted());
    QCOMPARE(added.count(), 1);
    window.close();
    QTRY_VERIFY(!window.isVisible());
}

void UiTests::verticalLayoutPreservesWorkflow()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("sine_source").nodeId;
    const auto left = doc.addChild(root, ChildSlot::Left, "demean").nodeId;
    const auto right = doc.addChild(root, ChildSlot::Right, "demean").nodeId;
    const auto down = doc.addChild(root, ChildSlot::Down, "demean").nodeId;
    const auto deep = doc.addChild(left, ChildSlot::Left, "demean").nodeId;
    const auto another = doc.addRoot("sine_source").nodeId;
    const auto before = workflowToJson(doc.model().definition());
    const auto layout = verticalWorkflowLayout(doc.model().definition());
    QCOMPARE(layout.size(), 6);
    QCOMPARE(layout[left].top(), layout[right].top());
    QCOMPARE(layout[left].top(), layout[down].top());
    QVERIFY(layout[left].center().x() < layout[down].center().x());
    QVERIFY(layout[down].center().x() < layout[right].center().x());
    QVERIFY(layout[deep].top() > layout[left].bottom());
    QVERIFY(layout[another].left() > layout[right].right());
    for (const auto &a : layout) for (const auto &b : layout)
        if (a != b) QVERIFY(!a.intersects(b));
    QCOMPARE(workflowToJson(doc.model().definition()), before);
    QVERIFY(doc.removeSubtree(left).ok());
    QVERIFY(doc.undo().ok());
    QCOMPARE(verticalWorkflowLayout(doc.model().definition()), layout);
    WorkflowDefinition longChain;
    NodeId parent;
    for (int i = 0; i < 10000; ++i) {
        NodeDefinition n{QUuid::createUuid(), "demean", 1, {}, std::nullopt, {}};
        if (!parent.isNull()) n.input = InputBinding{parent, "series", ChildSlot::Down};
        parent = n.id; longChain.nodes.push_back(n);
    }
    const auto deepLayout = verticalWorkflowLayout(longChain);
    QCOMPARE(deepLayout.size(), 10000);
    QCOMPARE(deepLayout[parent].top(), 9999. * 180.);
}
void UiTests::persistentAddControlsUseTheirOwnParent()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("sine_source").nodeId;
    const auto child = doc.addChild(root, ChildSlot::Down, "demean").nodeId;
    WorkflowCanvas canvas(&doc); canvas.resize(900, 700); canvas.show();
    canvas.setSelectedNode(root); canvas.fitWorkflow();
    QSignalSpy activations(&canvas, &WorkflowCanvas::slotActivated);
    QSignalSpy dropped(&canvas, &WorkflowCanvas::methodDropped);
    QPoint compact;
    for (auto *item : canvas.scene()->items())
        if (item->data(0).toInt() == 2 && item->data(2).toUuid() == child)
            compact = canvas.mapFromScene(item->boundingRect().center());
    QVERIFY(!compact.isNull());
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, compact);
    QCOMPARE(activations.count(), 1); QCOMPARE(activations[0][0].toUuid(), child);
    QCOMPARE(qvariant_cast<ChildSlot>(activations[0][1]), ChildSlot::Append);
    QMimeData mime; mime.setData(TaskMimeType, "demean");
    QDragEnterEvent enter(compact, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &enter);
    QDropEvent drop(compact, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &drop);
    QCOMPARE(dropped.count(), 1); QCOMPARE(dropped[0][1].toUuid(), child);
    const auto add = canvas.addViewportPosition(root);
    QVERIFY(canvas.mapToScene(add).x() > canvas.nodeRect(root).right());
    QVERIFY(canvas.mapToScene(add).y() < canvas.nodeRect(root).bottom());
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, add);
    QCOMPARE(activations.last()[0].toUuid(), root);
    QCOMPARE(qvariant_cast<ChildSlot>(activations.last()[1]), ChildSlot::Append);
    canvas.setSelectedNode(child);
    QCOMPARE(canvas.addViewportPosition(root), add);
    canvas.setEditable(false);
    for (auto *item : canvas.scene()->items()) QVERIFY(item->data(0).toInt() != 2);
}
void UiTests::exampleSourcePreviewAndLabels()
{
    setLanguage("zh");
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("odp1143_d18o").nodeId;
    ParameterEditor editor;
    editor.showNode(doc.node(root), doc.model().registry());
    connect(&editor, &ParameterEditor::applyRequested, &doc, [&] {
        doc.setParameters(root, editor.parameters()); editor.markClean();
    });
    auto *axis = editor.findChild<QComboBox *>("parameter_example_axis");
    QVERIFY(axis); QCOMPARE(axis->currentData().toString(), QString("age"));
    QVERIFY(editor.findChildren<QLabel *>().size() > 0);
    QCOMPARE(methodLabel(doc.model().registry().find("odp1143_d18o")->descriptor),
             QString::fromUtf8("ODP 1143 · 氧同位素 δ18O"));
    WorkflowPreviewController controller(&doc); controller.setActive(true); controller.selectNode(root);
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), WorkflowPreviewController::State::Ready, 10000);
    QCOMPARE(dataAs<SeriesData>(controller.result()->output.outputs["series"])->xAxis.unit, QString("ka BP"));
    axis->setCurrentIndex(axis->findData("depth"));
    QTRY_COMPARE(doc.node(root)->parameters["example_axis"].toString(), QString("depth"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.result() &&
        dataAs<SeriesData>(controller.result()->output.outputs["series"])->xAxis.unit == "mcd", 10000);
    QCOMPARE(controller.result()->output.report["example_source"].toObject()["doi"].toString(),
             QString("https://doi.org/10.1594/PANGAEA.132801"));
    const auto html = sourceProvenanceHtml(controller.result()->output.report);
    QVERIFY(html.contains("10.1594/PANGAEA.132801"));
    QVERIFY(html.contains("CC-BY-3.0"));
    QVERIFY(html.contains("1710"));
    setLanguage("en");
    QCOMPARE(methodLabel(doc.model().registry().find("sine_source")->descriptor), QString("Sine signal"));
    QCOMPARE(categoryLabel("ODP examples"), QString("ODP datasets"));
    setLanguage("zh");
}

void UiTests::syntheticSourcePreviewAndParameters()
{
    setLanguage("zh");
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("synthetic_orbital").nodeId;
    QVERIFY(!root.isNull());
    ParameterEditor editor;editor.resize(340,900);editor.showNode(doc.node(root),doc.model().registry());editor.show();
    QCOMPARE(editor.parameters(),doc.node(root)->parameters);
    connect(&editor,&ParameterEditor::applyRequested,&doc,[&]{doc.setParameters(root,editor.parameters());editor.markClean();});
    auto* mode=editor.findChild<QComboBox*>("parameter_synthetic_grid");
    auto* count=editor.findChild<QSpinBox*>("parameter_synthetic_count");
    auto* row=editor.findChild<QWidget*>("parameterRow_synthetic_count");
    QVERIFY(mode&&count&&row);QVERIFY(row->isHidden());
    QCOMPARE(methodLabel(doc.model().registry().find("synthetic_orbital")->descriptor),QString::fromUtf8("合成轨道代理信号"));
    WorkflowPreviewController controller(&doc);controller.setActive(true);controller.selectNode(root);
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(),WorkflowPreviewController::State::Ready,10000);
    QCOMPARE(controller.result()->output.outputs.size(),7);
    QCOMPARE(dataAs<SeriesData>(controller.result()->output.outputs["series"])->x.size(),2679);
    mode->setCurrentIndex(mode->findData("uniform"));QVERIFY(!row->isHidden());count->setValue(256);
    QTRY_COMPARE(doc.node(root)->parameters["synthetic_count"].toInt(),256);
    QTRY_VERIFY_WITH_TIMEOUT(controller.result()&&dataAs<SeriesData>(controller.result()->output.outputs["series"])->x.size()==256,10000);
    PlotView view;view.resize(800,500);view.show();
    const auto output=controller.result()->output;
    for(const auto& key:output.outputs.keys()){view.showOutput(output,key);QCoreApplication::processEvents();}
    const auto capture=qEnvironmentVariable("ASTER_CAPTURE_DIR");
    if(!capture.isEmpty()){QDir().mkpath(capture);editor.grab().save(capture+"/synthetic_parameters.png");view.showOutput(output,"series");QCoreApplication::processEvents();view.grab().save(capture+"/synthetic_proxy.png");}
    auto* min=editor.findChild<QDoubleSpinBox*>("parameter_synthetic_rate_min");QVERIFY(min);min->setValue(8);
    QTRY_COMPARE(doc.node(root)->parameters["synthetic_rate_min"].toDouble(),8.);
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(),WorkflowPreviewController::State::Failed,10000);
    min->setValue(1);QTRY_COMPARE_WITH_TIMEOUT(controller.state(),WorkflowPreviewController::State::Ready,10000);
    setLanguage("en");QCOMPARE(methodLabel(doc.model().registry().find("synthetic_orbital")->descriptor),QString("Synthetic orbital proxy"));setLanguage("zh");
}

void UiTests::sourceNodePreviewBeforeAddingAnalysis()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("source.csv");
    QFile input(path); QVERIFY(input.open(QIODevice::WriteOnly));
    input.write("x,y\n0,1\n1,2\n2,4\n3,8\n"); input.close();
    AppSettings settings; settings.databasePath = dir.filePath("results.sqlite");
    MainWindow window(settings, false); window.resize(1550, 950); window.show();
    auto *sources = window.findChild<SourcePanel *>(); sources->addFiles({path});
    QTRY_VERIFY(sources->readyToRun());
    const auto root = sources->root(); window.canvas()->nodeSelected(root);
    auto *preview = window.findChild<WorkflowPreviewPanel *>();
    auto *controller = preview->controller();
    auto *plot = preview->findChild<PlotView *>("nodePreviewPlot");
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QCOMPARE(controller->node(), root);
    QCOMPARE(controller->result()->executed, QVector<NodeId>{root});
    QVERIFY(plot->hasPlot());
    QCOMPARE(dataAs<SeriesData>(plot->currentData())->y, QVector<double>({1, 2, 4, 8}));
    QVERIFY(!window.runner()->busy());
    ResultRepository repository; QVERIFY(repository.open(settings.databasePath).isEmpty());
    QCOMPARE(repository.list().records.size(), 0); // Preview is not a saved run.

    const auto added = window.document()->addChild(root, ChildSlot::Down, "affine", {{"factor", 2}});
    QVERIFY(added.ok());
    window.canvas()->nodeSelected(added.nodeId);
    QCOMPARE(controller->node(), added.nodeId);
    QVERIFY(!controller->result());
    QVERIFY(!plot->hasPlot()); // Never display the source graph under the new method's name.
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QCOMPARE(controller->result()->executed, QVector<NodeId>({root, added.nodeId}));
    QCOMPARE(dataAs<SeriesData>(plot->currentData())->y, QVector<double>({2, 4, 8, 16}));
    QCOMPARE(repository.list().records.size(), 0);

    controller->setEditing(true);
    QVERIFY(!plot->hasPlot());
    controller->setEditing(false);
    window.canvas()->nodeSelected(root); // A queued child preview must not replace this selection.
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QCOMPARE(controller->result()->executed, QVector<NodeId>{root});
    QCOMPARE(dataAs<SeriesData>(plot->currentData())->y, QVector<double>({1, 2, 4, 8}));
}

void UiTests::cocoWorkBudgetPresentation()
{
    for (const auto *language : {"zh", "en"})
    {
        setLanguage(language);
        for (const auto *code : {"COCO_MC_WORK_BUDGET", "COCO_WINDOW_WORK_BUDGET"})
        {
            ErrorInfo error{code, {{"estimated_work", 6000000000.},
                                  {"maximum_work", 5000000000.},
                                  {"requested_iterations", 5000},
                                  {"maximum_iterations", 4166}}, {}};
            const auto shown = presentError(error);
            QVERIFY(shown.summary.contains("6000000000"));
            QVERIFY(shown.summary.contains("5000000000"));
            QVERIFY(shown.suggestion.contains("4166"));
            QVERIFY(shown.suggestion.contains("5000"));
            QVERIFY(!shown.suggestion.contains('{'));
            QVERIFY(!shown.details.contains("131072"));
        }
    }
    setLanguage("zh");
}

void UiTests::frequencyLimitTypingPreservesDecimals_data()
{
    QTest::addColumn<QString>("method");
    for (const auto *key : {"mtm", "evolution_fft", "evolution_mtm"})
        QTest::newRow(key) << QString(key);
}
void UiTests::frequencyLimitTypingPreservesDecimals()
{
    QFETCH(QString, method);
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("sine_source").nodeId;
    const auto node = doc.addChild(root, ChildSlot::Down, method,
                                  {{"frequency_max", .2}}).nodeId;
    ParameterEditor editor; editor.resize(330, 900); editor.show(); editor.activateWindow();
    editor.showNode(doc.node(node), doc.model().registry());
    auto *limit = editor.findChild<QDoubleSpinBox *>("parameter_frequency_max");
    QVERIFY(limit);
    auto *input = limit->findChild<QLineEdit *>();
    QVERIFY(input);
    QTest::mouseClick(input, Qt::LeftButton);
    QTRY_VERIFY(limit->hasFocus());
    input->selectAll();
    QTest::keyClicks(input, "0");
    QTest::qWait(300); // Longer than the parameter debounce: zero must remain editable.
    QCOMPARE(input->text(), QString("0"));
    QTest::keyClicks(input, ".");
    QCOMPARE(input->text(), QString("0."));
    // Another parameter's visibility refresh must not overwrite a partial decimal.
    auto *changedField = editor.findChild<QDoubleSpinBox *>(method == "mtm" ? "parameter_nw" : "parameter_evo_frequency_ratio");
    QVERIFY(changedField); changedField->setValue(method == "mtm" ? 3 : .8);
    QCOMPARE(input->text(), QString("0."));
    QTest::keyClicks(input, "01");
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(limit->value(), .01);
    QCOMPARE(editor.parameters()["frequency_max"].toDouble(), .01);
    input->selectAll(); QTest::keyClicks(input, "0");
    QTest::keyClick(input, Qt::Key_Tab);
    QCOMPARE(limit->value(), 0.);
    QCOMPARE(input->text(), QString("0"));
    QTest::mouseClick(input, Qt::LeftButton);
    input->selectAll(); QTest::keyClicks(input, "0.125");
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(limit->value(), .125);
}

void UiTests::legacyFrequencyZeroCommit_data()
{
    QTest::addColumn<QString>("method");
    for (const auto *key : {"mtm", "evolution_fft", "evolution_mtm"})
        QTest::newRow(key) << QString(key);
}
void UiTests::legacyFrequencyZeroCommit()
{
    QFETCH(QString, method);
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("sine_source").nodeId;
    const QString legacyKey = method == "mtm" ? "fmax_ratio" : "evo_frequency_ratio";
    const auto node = doc.addChild(root, ChildSlot::Down, method, {{legacyKey, .3}}).nodeId;
    ParameterEditor editor; editor.resize(300, 650); editor.show(); editor.activateWindow();
    for (const auto key : {Qt::Key_Return, Qt::Key_Tab})
    {
        editor.showNode(doc.node(node), doc.model().registry());
        auto *limit = editor.findChild<QDoubleSpinBox *>("parameter_frequency_max");
        auto *legacy = editor.findChild<QDoubleSpinBox *>("parameter_" + legacyKey);
        auto *input = limit->findChild<QLineEdit *>();
        QVERIFY(input && legacy);
        QTest::mouseClick(input, Qt::LeftButton);
        QTRY_VERIFY(limit->hasFocus());
        QTest::keyClick(input, Qt::Key_Tab); // Merely visiting the field keeps the old cutoff.
        QCOMPARE(editor.parameters()[legacyKey].toDouble(), .3);
        QTest::mouseClick(input, Qt::LeftButton);
        QTRY_VERIFY(limit->hasFocus());
        input->selectAll(); QTest::keyClicks(input, "0");
        QTest::keyClick(input, key);
        QCOMPARE(editor.parameters()["frequency_max"].toDouble(), 0.);
        QCOMPARE(editor.parameters()[legacyKey].toDouble(), 1.);
        QVERIFY(limit->specialValueText().isEmpty());
        QVERIFY(editor.findChild<QLabel *>("parameterLabel_frequency_max")->text().contains(text("ui.frequency_zero_hint")));
        QVERIFY(editor.dirty());
        QVERIFY(!legacy->isVisible());
    }
}

void UiTests::evolutionSettingsAndPlotDefaults()
{
    const auto registry = makeScientificRegistry();
    for (const auto *language : {"zh", "en"})
    for (const auto *method : {"evolution_fft", "evolution_mtm"})
    {
        setLanguage(language);
        WorkflowDocument doc(registry);
        const auto root = doc.addRoot("sine_source").nodeId;
        const auto node = doc.addChild(root, ChildSlot::Down, method).nodeId;
        ParameterEditor editor; editor.resize(340, 1000); editor.show();
        editor.showNode(doc.node(node), registry);
        auto *limit = editor.findChild<QDoubleSpinBox *>("parameter_frequency_max");
        auto *legacy = editor.findChild<QDoubleSpinBox *>("parameter_evo_frequency_ratio");
        auto *normalize = editor.findChild<QCheckBox *>("parameter_evo_normalize");
        QVERIFY(limit && legacy && normalize);
        QVERIFY(!legacy->isVisible());
        QCOMPARE(normalize->isChecked(),QString(method)=="evolution_fft");
        normalize->setChecked(false); // Explicit physical PSD comparison.
        QVERIFY(editor.findChild<QLabel *>("parameterLabel_frequency_max")->toolTip().isEmpty());
        QVERIFY(limit->accessibleDescription().contains("1/m"));
        QVERIFY(editor.findChild<QSpinBox *>("parameter_evo_step")->accessibleDescription().contains("37"));
        limit->setValue(1);
        SeriesData s; s.xAxis = {"Depth", "m"}; s.yAxis = {"Proxy", "u"};
        for (int i = 0; i < 256; ++i) { s.x << .2 * i; s.y << std::sin(.2 * i); }
        auto task = registry.create(method, editor.parameters());
        QVERIFY(task);
        const auto raw = task->execute({makeData(s)}, {});
        QCOMPARE(raw.state, NodeState::Succeeded);
        QTest::mouseClick(normalize, Qt::LeftButton);
        QVERIFY(editor.parameters()["evo_normalize"].toBool());
        task = registry.create(method, editor.parameters());
        const auto relative = task->execute({makeData(s)}, {});
        QCOMPARE(relative.state, NodeState::Succeeded);
        PlotView view;
        view.showOutput(raw, "power_map");
        QCOMPARE(view.style().colorInterpolate,QString(method)=="evolution_fft");
        QVERIFY(!view.style().colorLog);
        view.showOutput(relative, "power_map");
        QCOMPARE(view.style().colorInterpolate,QString(method)=="evolution_fft");
        QVERIFY(!view.style().colorLog);
        QCOMPARE(dataAs<GridData>(relative.outputs["power_map"])->valueAxis.unit, QString("1"));
        QVERIFY(view.style().y.label.contains("Frequency"));
        const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
        if (!capture.isEmpty())
        {
            QDir().mkpath(capture);
            QVERIFY(editor.grab().save(capture + "/evolution_settings_" + language + "_" + method + ".png"));
        }
    }
    setLanguage("zh");
}
void UiTests::inputListDistinguishesHoverAndSelection_data()
{
    QTest::addColumn<QString>("theme");
    QTest::newRow("light") << QStringLiteral("light");
    QTest::newRow("dark") << QStringLiteral("dark");
}
void UiTests::inputListDistinguishesHoverAndSelection()
{
    QFETCH(QString, theme);
    auto &themes = ThemeManager::instance();
    const auto previous = themes.name();
    const auto restore = qScopeGuard([&] { themes.apply(*qApp, previous); });
    QVERIFY(themes.apply(*qApp, theme).isEmpty());
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("file_source").nodeId;
    doc.setFiles(root, {"A.csv", "B.csv", "C.csv", "D.csv", "E.csv", "F.csv"});
    SourcePanel panel(&doc);
    panel.resize(320, 600);
    panel.show();
    auto *list = panel.findChild<QListWidget *>("sourceFiles");
    QVERIFY(list && !list->alternatingRowColors());
    auto *view = list->viewport();
    const auto row = [&](int n) { return list->visualItemRect(list->item(n)); };
    QTRY_VERIFY(row(4).isValid() && view->rect().contains(row(4).center()));
    const auto fill = [&](int n) { return viewColorAt(view, {row(n).right() - 20, row(n).center().y()}); };
    const auto mark = [&](int n) { return viewColorAt(view, {row(n).left() + 3, row(n).center().y()}); };
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, row(1).center());
    QTest::mouseMove(view, row(3).center());
    QTRY_COMPARE(fill(3), themeColor("fileListHover"));
    QCOMPARE(fill(1), themeColor("fileListSelection"));
    QCOMPARE(mark(1), themeColor("fileListMark"));
    QCOMPARE(fill(4), themeColor("navigation"));
    QCOMPARE(fill(5), fill(4));
    QCOMPARE(viewColorAt(view, {row(4).center().x(), row(4).top()}), themeColor("navigation"));
    QCOMPARE(viewColorAt(view, {row(4).left() + 7, row(4).top()}), themeColor("navigation"));
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(list->selectedItems().size(), 1);
    QTest::mouseMove(view, row(1).center());
    QTRY_COMPARE(fill(3), themeColor("navigation"));
    QCOMPARE(fill(1), themeColor("fileListSelection"));
    QCOMPARE(mark(1), themeColor("fileListMark"));
    QTest::mouseClick(view, Qt::LeftButton, Qt::ShiftModifier, row(3).center());
    QTest::mouseMove(view, row(4).center());
    panel.findChild<QPushButton *>("importFiles")->setFocus();
    QCOMPARE(list->selectedItems().size(), 3);
    for (int n = 1; n <= 3; ++n) {
        QCOMPARE(fill(n), themeColor("fileListSelection"));
        QCOMPARE(mark(n), themeColor(n == list->currentRow() ? "fileListMark" : "fileListSelection"));
    }
    list->setFocus(Qt::TabFocusReason);
    QTest::keyClick(list, Qt::Key_Up);
    QCOMPARE(list->currentRow(), 2);
    QCOMPARE(list->selectedItems().size(), 1);
    QCOMPARE(mark(2), themeColor("fileListMark"));
    QTest::keyClick(list, Qt::Key_Down, Qt::ControlModifier);
    QCOMPARE(list->currentRow(), 3);
    QCOMPARE(list->selectedItems(), QList<QListWidgetItem *>{list->item(2)});
    QCOMPARE(mark(3), themeColor("fileListMark"));
    QCOMPARE(fill(3), themeColor("navigation"));
    QCOMPARE(mark(2), themeColor("fileListSelection"));
}

void UiTests::resultListSeparatesOpenCheckAndHover_data()
{
    inputListDistinguishesHoverAndSelection_data();
}
void UiTests::resultListSeparatesOpenCheckAndHover()
{
    QFETCH(QString, theme);
    auto &themes = ThemeManager::instance();
    const auto previous = themes.name();
    const auto restore = qScopeGuard([&] { themes.apply(*qApp, previous); });
    QVERIFY(themes.apply(*qApp, theme).isEmpty());
    QTemporaryDir dir;
    const QString path = dir.filePath("files.sqlite");
    {
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        for (int i = 5; i >= 1; --i)
            QVERIFY(repo.save(storedRun(QString::number(80000 + i) + ".csv")).saved());
    }
    ResultsPage page(makeScientificRegistry());
    page.resize(1100, 720);
    page.show();
    page.activateWindow();
    page.setDatabase(path);
    auto *list = page.findChild<QListWidget *>("resultRecords");
    auto *search = page.findChild<QLineEdit *>("recordSearch");
    auto *all = page.findChild<QPushButton *>("checkAllRecords");
    QVERIFY(list && search && all);
    QTRY_COMPARE(list->count(), 5);
    QTRY_VERIFY(all->isEnabled());
    auto *view = list->viewport();
    const auto rect = [&](int row) { return list->visualItemRect(list->item(row)); };
    const auto fill = [&](int row) { return viewColorAt(view, {rect(row).right() - 20, rect(row).center().y()}); };
    const auto mark = [&](int row) { return viewColorAt(view, {rect(row).left() + 3, rect(row).center().y()}); };
    const auto hasFocusLine = [&](int row) {
        const QRect strip(rect(row).left() + 40, rect(row).top(), 32, 3);
        const auto pixels = view->grab(strip).toImage();
        const auto background = fill(row);
        for (int y = 0; y < pixels.height(); ++y)
            for (int x = 0; x < pixels.width(); ++x)
                if (pixels.pixelColor(x, y) != background) return true;
        return false;
    };
    const qint64 a = list->item(0)->data(RecordRole::Id).toLongLong();
    const qint64 b = list->item(1)->data(RecordRole::Id).toLongLong();
    QCOMPARE(list->item(0)->text(), QString("80001.csv"));
    QCOMPARE(list->item(1)->text(), QString("80002.csv"));
    QVERIFY(!list->alternatingRowColors());
    QTest::mouseMove(view, rect(2).center());
    QCOMPARE(fill(3), fill(4));
    QCOMPARE(viewColorAt(view, {rect(3).right() - 20, rect(3).top()}), themeColor("navigation"));

    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, rect(0).center());
    list->setCurrentRow(1);
    QTest::keyClick(list, Qt::Key_Space);
    QTRY_COMPARE(page.recordId(), a);
    QTest::mouseMove(view, rect(3).center());
    QTest::mouseMove(view, rect(2).center());
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(list->item(1)->checkState(), Qt::Checked);
    QCOMPARE(mark(0), themeColor("fileListMark"));
    QCOMPARE(fill(0), themeColor("fileListSelection"));
    QCOMPARE(mark(1), themeColor("navigation"));
    QTRY_COMPARE(fill(2), themeColor("fileListHover"));
    QVERIFY(hasFocusLine(1)); // Space entered batch selection from the keyboard.
    QTest::keyClick(list, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 2);
    QVERIFY(hasFocusLine(2));
    QCOMPARE(mark(0), themeColor("fileListMark"));
    QTest::keyClick(list, Qt::Key_Up);
    QTest::keyClick(list, Qt::Key_Space);
    QCOMPARE(list->item(1)->checkState(), Qt::Unchecked);
    QCOMPARE(page.recordId(), a);
    QTest::keyClick(list, Qt::Key_Space);
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, RecordItemDelegate::checkRect(rect(3)).center());
    QCOMPARE(list->item(1)->checkState(), Qt::Checked);
    QVERIFY(!hasFocusLine(3));
    list->setCurrentRow(1);

    all->click();
    QTRY_COMPARE(list->item(4)->checkState(), Qt::Checked);
    QTRY_VERIFY(all->isEnabled());
    search->setFocus();
    QCOMPARE(mark(0), themeColor("fileListMark"));
    QCOMPARE(fill(4), themeColor("navigation"));
    page.refresh();
    QTRY_VERIFY(!page.databaseBusy());
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(mark(0), themeColor("fileListMark"));
    search->setText("80002");
    QTRY_COMPARE(list->count(), 1);
    QCOMPARE(page.recordId(), a);
    QCOMPARE(list->item(0)->data(RecordRole::Id).toLongLong(), b);
    QVERIFY(mark(0) != themeColor("fileListMark"));
    search->clear();
    QTRY_COMPARE(list->count(), 5);
    QCOMPARE(mark(0), themeColor("fileListMark"));
    QCOMPARE(list->currentRow(), 1);
    list->setFocus(Qt::TabFocusReason);
    QTest::keyClick(list, Qt::Key_Return);
    QTRY_COMPARE(page.recordId(), b);
    QCOMPARE(mark(1), themeColor("fileListMark"));
    QVERIFY(mark(0) != themeColor("fileListMark"));
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);

    QSignalSpy messages(&page, &ResultsPage::message);
    page.openRecord(1000);
    QTRY_VERIFY(!messages.isEmpty() && !messages.last().first().toString().isEmpty());
    QCOMPARE(page.recordId(), qint64(0));
    QVERIFY(mark(1) != themeColor("fileListMark"));
}
void UiTests::compactInputStatesAndFrequencyEditor()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("file_source").nodeId;
    const QString path = QDir::temp().absoluteFilePath("Geological series & reference/Section 04/Source data/"
                                                       "Cau-GR-sue-rsp0.02-28.732-LOWESS.csv");
    doc.setFiles(root, {path, "ODP-1143.csv", "series.csv"});
    SourcePanel panel(&doc); panel.resize(290, 650); panel.show(); panel.selectRoot(root);
    auto *list = panel.findChild<QListWidget *>("sourceFiles");
    QVERIFY(list); QCOMPARE(list->count(), 3);
    auto *item = list->item(0);
    for (const auto state : {SourceFileState::Pending, SourceFileState::Running, SourceFileState::Saved,
                            SourceFileState::Unsaved, SourceFileState::Cancelled, SourceFileState::Failed})
    {
        panel.setFileState(root, path, state);
        QCOMPARE(item->data(Qt::UserRole + 2).toInt(), int(state));
        const auto tooltip = unwrappedFileToolTip(item);
        QVERIFY2(tooltip.contains(path), qPrintable(tooltip));
        QVERIFY2(tooltip.contains(item->data(Qt::UserRole + 1).toString()), qPrintable(tooltip));
        QVERIFY(item->data(Qt::AccessibleDescriptionRole).toString().contains(path));
        QCOMPARE(item->text(), QFileInfo(path).fileName());
    }
    panel.setFileState(root, path, SourceFileState::Running);
    panel.setFileState(root, list->item(1)->data(Qt::UserRole).toString(), SourceFileState::Saved);
    panel.refresh(); item = list->item(0);
    QCOMPARE(item->data(Qt::UserRole + 2).toInt(), int(SourceFileState::Running));
    const auto mtm = doc.addChild(root, ChildSlot::Down, "mtm", {{"fmax_ratio", .3}}).nodeId;
    ParameterEditor editor; editor.resize(290, 650); editor.show();
    editor.showNode(doc.node(mtm), doc.model().registry());
    auto *limit = editor.findChild<QDoubleSpinBox *>("parameter_frequency_max");
    auto *legacy = editor.findChild<QDoubleSpinBox *>("parameter_fmax_ratio");
    QVERIFY(limit && legacy); QVERIFY(legacy->isVisible());
    QCOMPARE(editor.parameters()["fmax_ratio"].toDouble(), .3);
    QVERIFY(limit->specialValueText().isEmpty());
    QVERIFY(editor.findChild<QLabel *>("parameterLabel_frequency_max")->text().contains(text("ui.frequency_legacy")));
    limit->setValue(.08);
    QCOMPARE(editor.parameters()["fmax_ratio"].toDouble(), 1.);
    QVERIFY(!legacy->isVisible());
    limit->setValue(0);
    QVERIFY(limit->specialValueText().isEmpty());
    QVERIFY(editor.findChild<QLabel *>("parameterLabel_frequency_max")->text().contains(text("ui.frequency_zero_hint")));
    const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    for (const auto *theme : {"light", "dark"})
    {
        ThemeManager::instance().apply(*qApp, theme);
        QCoreApplication::processEvents();
        // Adding the MTM node refreshes SourcePanel and replaces its list items.
        // Reacquire the item instead of dereferencing the previous model entry.
        item = list->item(0); QVERIFY(item);
        // The delegate uses the list's font, not the surrounding panel's font.
        // Respect its 32px minimum when the platform supplies a small font.
        QTRY_VERIFY2(list->visualItemRect(item).height() <= std::max(32, list->fontMetrics().height() + 18),
                 qPrintable(QString("row=%1 listFont=%2 panelFont=%3")
                     .arg(list->visualItemRect(item).height()).arg(list->fontMetrics().height()).arg(panel.fontMetrics().height())));
        if (!capture.isEmpty())
        {
            QDir().mkpath(capture);
            QVERIFY(panel.grab().save(capture + "/input_" + theme + ".png"));
            QVERIFY(editor.grab().save(capture + "/mtm_" + theme + ".png"));
        }
    }
    ThemeManager::instance().apply(*qApp, "light");
}

void UiTests::visibleSelectionActionsPreserveFilterScope()
{
    QTemporaryDir dir; const auto path = dir.filePath("selection.sqlite");
    qint64 keep, remove;
    {
        ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
        keep = repo.save(storedRun("keep.csv")).recordId;
        remove = repo.save(storedRun("remove.csv")).recordId;
    }
    RecordDirectoryPanel panel; panel.resize(280, 620); panel.show(); panel.setDatabase(path);
    QTRY_COMPARE(panel.recordCount(), 2);
    auto *all = panel.findChild<QPushButton *>("checkAllRecords");
    auto *matches = panel.findChild<QPushButton *>("checkMatchingRecords");
    auto *search = panel.findChild<QLineEdit *>("recordSearch");
    auto *summary = panel.findChild<QLabel *>("recordSelectionCount");
    auto *list = panel.findChild<QListWidget *>("resultRecords");
    QVERIFY(all && matches && search && summary && list);
    QVERIFY(all->isCheckable() && matches->isCheckable());
    QVERIFY(!all->isChecked() && !matches->isChecked());
    QVERIFY(!list->alternatingRowColors());
    QCOMPARE(summary->text(), text("ui.records_selected_of").arg(0).arg(2));
    search->setText("keep.csv");
    QTRY_COMPARE(panel.recordCount(), 1);
    QTRY_VERIFY(all->isEnabled() && matches->isEnabled());
    all->click();
    QTRY_COMPARE(panel.checkedRecords().size(), 2);
    QTRY_VERIFY(matches->isEnabled());
    QVERIFY(all->isChecked() && !matches->isChecked());
    QCOMPARE(summary->text(), text("ui.records_selected_of").arg(2).arg(2));
    list->setCurrentRow(0);
    QPersistentModelIndex current = list->currentIndex();
    QSignalSpy resets(list->model(), &QAbstractItemModel::modelReset);
    matches->click();
    QTRY_VERIFY(matches->isChecked() && matches->isEnabled());
    QCOMPARE(panel.checkedRecords().size(), 2);
    QVERIFY(all->isChecked());
    QCOMPARE(resets.size(), 0);
    QVERIFY(current.isValid() && current == list->currentIndex());
    matches->click();
    QTRY_COMPARE(panel.checkedRecords().size(), 1);
    QTRY_VERIFY(matches->isEnabled());
    QVERIFY(panel.checkedRecords().contains(remove));
    QVERIFY(!panel.checkedRecords().contains(keep));
    QVERIFY(all->isChecked() && !matches->isChecked());
    QCOMPARE(summary->text(), text("ui.records_selected_of").arg(1).arg(2));
    list->item(0)->setCheckState(Qt::Checked);
    QCOMPARE(panel.checkedRecords().size(), 2);
    QVERIFY(all->isChecked() && !matches->isChecked());
    matches->click();
    QTRY_VERIFY(matches->isChecked() && all->isEnabled());
    all->click();
    QCOMPARE(panel.checkedRecords().size(), 0);
    QVERIFY(!all->isChecked() && matches->isChecked());
    search->setText("absent");
    QTRY_COMPARE(panel.recordCount(), 0);
    QTRY_VERIFY(all->isEnabled());
    QVERIFY(matches->isChecked() && !matches->isEnabled());
    QCOMPARE(summary->text(), text("ui.records_selected_of").arg(0).arg(2));
    all->click();
    QTRY_COMPARE(panel.checkedRecords().size(), 2);
    QTRY_VERIFY(all->isEnabled());
    QCOMPARE(summary->text(), text("ui.records_selected_of").arg(2).arg(2));
    search->clear();
    QTRY_COMPARE(panel.recordCount(), 2);
    QTRY_VERIFY(matches->isEnabled());
    QVERIFY(all->isChecked() && matches->isChecked());
    matches->click();
    QTRY_COMPARE(panel.checkedRecords().size(), 0);
    QTRY_VERIFY(all->isEnabled());
    QVERIFY(all->isChecked() && !matches->isChecked());
    list->setCurrentRow(0);
    const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) QDir().mkpath(capture);
    for (const auto *language : {"zh", "en"})
    {
        setLanguage(language);
        RecordDirectoryPanel localized; localized.resize(280, 620); localized.show();
        localized.setDatabase(path); QTRY_COMPARE(localized.recordCount(), 2);
        localized.findChild<QListWidget *>("resultRecords")->setCurrentRow(0);
        for (const auto *theme : {"light", "dark"})
        {
            ThemeManager::instance().apply(*qApp, theme); QCoreApplication::processEvents();
            QVERIFY(localized.width() <= 280);
            if (!capture.isEmpty())
                QVERIFY(localized.grab().save(capture + "/directory_" + language + "_" + theme + ".png"));
        }
    }
    setLanguage("zh"); ThemeManager::instance().apply(*qApp, "light");
}

void UiTests::axisHintsPreserveSingleAxisNavigation()
{
    QCustomPlot plot; new PlotAxisHints(&plot);
    plot.resize(520, 360); plot.setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot.xAxis->setRange(1, 10); plot.yAxis->setRange(1, 20);
    plot.xAxis->setLabel("Frequency"); plot.yAxis->setLabel("Power");
    plot.show(); QCoreApplication::processEvents(); plot.replot();
    for (auto *axis : {plot.xAxis, plot.yAxis})
    {
        const auto rect = plot.axisRect()->rect();
        const auto at = axis == plot.xAxis ? QPoint(rect.center().x(), rect.bottom() + 10)
                                          : QPoint(rect.left() - 10, rect.center().y());
        QVERIFY(axis->selectTest(at, false) >= 0);
        QHelpEvent help(QEvent::ToolTip, at, plot.mapToGlobal(at));
        QApplication::sendEvent(&plot, &help);
        QCOMPARE(QToolTip::text(), text(axis == plot.xAxis ? "ui.axis_hover_x" : "ui.axis_hover_y"));
        const auto before = axis->range();
        auto *other = axis == plot.xAxis ? plot.yAxis : plot.xAxis;
        const auto otherBefore = other->range();
        QWheelEvent wheel(at, plot.mapToGlobal(at), {}, {0, 120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&plot, &wheel);
        QVERIFY(axis->range().size() < before.size());
        QCOMPARE(other->range().lower, otherBefore.lower);
        QCOMPARE(other->range().upper, otherBefore.upper);
        const auto beforeDrag = axis->range();
        const auto to = at + (axis == plot.xAxis ? QPoint(20, 0) : QPoint(0, 20));
        QTest::mousePress(&plot, Qt::LeftButton, Qt::NoModifier, at);
        QMouseEvent move(QEvent::MouseMove, to, plot.mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&plot, &move);
        QTest::mouseRelease(&plot, Qt::LeftButton, Qt::NoModifier, to);
        QVERIFY(axis->range().lower != beforeDrag.lower);
        QCOMPARE(other->range().lower, otherBefore.lower);
    }
    QToolTip::hideText();
}

void UiTests::fileReadinessSurvivesDisabledPanel()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("input.csv");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("time,A,B\n0,1,10\n1,2,20\n2,3,30\n");
    file.close();
    WorkflowDocument document(makeScientificRegistry());
    SourcePanel panel(&document);
    panel.setEnabled(false);
    QSignalSpy inputs(&panel, &SourcePanel::previewInputChanged);
    QSignalSpy sources(&panel, &SourcePanel::previewSourceChanged);
    panel.addFiles({path});
    auto *options = panel.findChild<FileOptionsEditor *>();
    auto *column = options->findChild<QComboBox *>("fileYColumn");
    QTRY_COMPARE(column->count(), 3);
    QVERIFY(!panel.isEnabled());
    QVERIFY(options->ready());
    QVERIFY(panel.readyToRun());
    QVERIFY(inputs.last()[2].toBool());
    QVERIFY(sources.last()[2].toBool());
    panel.setEnabled(true);
    inputs.clear(); sources.clear();
    panel.selectRoot(panel.root());
    QCOMPARE(inputs.count(), 1);
    QCOMPARE(sources.count(), 1);
    QVERIFY(inputs.last()[2].toBool());
    const auto before = document.sources();
    panel.syncPreviewInput();
    QCOMPARE(document.files(panel.root()), QStringList({path}));
    QCOMPARE(document.sources().value(panel.root()).front().provenance,
             before.value(panel.root()).front().provenance);
    options->setFile(dir.filePath("missing.csv"), {});
    QVERIFY(!options->ready());
    QVERIFY(!inputs.last()[2].toBool());
    options->setFile(path, {});
    QTRY_VERIFY(options->ready());
    QVERIFY(inputs.last()[2].toBool());
}

void UiTests::previewRecoversAfterRunAndRereadsFile()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("wave.csv");
    const auto writeInput = [&](double amplitude) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        QByteArray bytes("time,value\n");
        for (int i = 0; i < 128; ++i)
            bytes += QByteArray::number(i) + ',' +
                     QByteArray::number(amplitude * (2 + std::sin(i * .25)), 'g', 15) + '\n';
        return file.write(bytes) == bytes.size();
    };
    QVERIFY(writeInput(1));
    AppSettings settings; settings.databasePath = dir.filePath("results.sqlite");
    MainWindow window(settings, false);
    window.resize(1550, 950); window.show();
    auto *sources = window.findChild<SourcePanel *>();
    sources->addFiles({path});
    QTRY_VERIFY(sources->readyToRun());
    const auto root = sources->root();
    const auto added = window.document()->addChild(root, ChildSlot::Down, "mtm",
                                                   {{"ar1", false}, {"swa", false}});
    QVERIFY(added.ok());
    window.canvas()->nodeSelected(added.nodeId);
    auto *controller = window.findChild<WorkflowPreviewPanel *>()->controller();
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QSignalSpy batches(window.runner(), &RunController::batchReady);
    qint64 previousRecord = 0;
    for (int run = 0; run < 2; ++run)
    {
        if (run) QVERIFY(writeInput(10));
        window.startRun();
        QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy() && batches.count() == run + 1, 15000);
        const auto batch = qvariant_cast<std::shared_ptr<const BatchResult>>(batches.last()[0]);
        QVERIFY(batch && batch->error.isEmpty());
        QCOMPARE(batch->items.size(), 1);
        const auto &item = batch->items.front();
        QCOMPARE(item.computation, RunState::Succeeded);
        QVERIFY(item.persistence.saved());
        QVERIFY(item.persistence.recordId > previousRecord);
        previousRecord = item.persistence.recordId;
        ResultRepository repo;
        QVERIFY(repo.open(settings.databasePath).isEmpty());
        const auto loaded = repo.load(previousRecord);
        QVERIFY(loaded.ok());
        const auto *input = dataAs<SeriesData>(loaded.run.nodes[root].outputs["series"]);
        QVERIFY(input);
        QCOMPARE(input->y.front(), run ? 20. : 2.);
        QCOMPARE(repo.list().records.size(), run + 1);
        QCOMPARE(window.document()->files(root), QStringList({path}));
        window.setPage(0);
        QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
        QVERIFY(window.findChild<PlotView *>("nodePreviewPlot")->hasPlot());
        window.canvas()->nodeSelected(root);
        QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
        QCOMPARE(dataAs<SeriesData>(controller->result()->output.outputs["series"])->y.front(),
                 run ? 20. : 2.);
        window.canvas()->nodeSelected(added.nodeId);
        QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    }
}

void UiTests::wheelRequiresDeliberateNumericFocus()
{
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *outside = new QLineEdit;
    layout->addWidget(outside);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *content = new QWidget;
    auto *fields = new QVBoxLayout(content);
    auto *integer = new IntegerEditor;
    auto *decimal = new DecimalEditor;
    auto *choice = new ChoiceEditor;
    integer->setRange(-100, 100); integer->setValue(10);
    decimal->setRange(-100, 100); decimal->setValue(1.5); decimal->setSingleStep(.25);
    for (int i = 0; i < 40; ++i) choice->addItem(QString::number(i));
    fields->addWidget(integer); fields->addWidget(decimal); fields->addWidget(choice);
    fields->addSpacing(1500);
    scroll->setWidget(content); layout->addWidget(scroll);
    window.resize(360, 480); window.show(); window.activateWindow();
    const auto wheel = [](QWidget *target, int delta) {
        const QPointF point(target->rect().center());
        QWheelEvent event(point, target->mapToGlobal(point.toPoint()), {}, {0, delta},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        // Qt propagates ignored native wheel events to ancestors. Deliver that
        // path in logical widget coordinates, independent of screen scaling.
        QSpontaneKeyEvent::setSpontaneous(&event);
        qApp->notify(target, &event);
        QCoreApplication::processEvents();
    };
    outside->setFocus(); QTRY_VERIFY(outside->hasFocus());
    wheel(integer->findChild<QLineEdit *>(), -120);
    QCOMPARE(integer->value(), 10);
    QVERIFY(outside->hasFocus());
    QVERIFY(scroll->verticalScrollBar()->value() > 0);
    scroll->verticalScrollBar()->setValue(0);
    integer->setFocus(Qt::OtherFocusReason); // Automatic focus is not editing intent.
    QTRY_VERIFY(integer->hasFocus());
    wheel(integer, 120); QCOMPARE(integer->value(), 10);
    QTest::mouseClick(integer->findChild<QLineEdit *>(), Qt::LeftButton);
    wheel(integer->findChild<QLineEdit *>(), 120); QCOMPARE(integer->value(), 11);
    QTest::keyClick(integer, Qt::Key_Tab);
    QTRY_VERIFY(decimal->hasFocus());
    wheel(decimal->findChild<QLineEdit *>(), 120); QCOMPARE(decimal->value(), 1.75);
    outside->setFocus(); QTRY_VERIFY(outside->hasFocus());
    wheel(decimal, -120); QCOMPARE(decimal->value(), 1.75);
    scroll->verticalScrollBar()->setValue(0);
    choice->setFocus(Qt::TabFocusReason);
    wheel(choice, -120); QCOMPARE(choice->currentIndex(), 0);
    QVERIFY(scroll->verticalScrollBar()->value() > 0);
    scroll->verticalScrollBar()->setValue(0);
    choice->showPopup();
    QTRY_VERIFY(choice->view()->isVisible());
    wheel(choice->view()->viewport(), -120);
    QVERIFY(choice->view()->verticalScrollBar()->value() > 0);
    choice->hidePopup();
    QTest::keyClick(choice, Qt::Key_Down);
    QCOMPARE(choice->currentIndex(), 1);
    // The scientific plot's wheel zoom remains independent of form controls.
    PlotView plotView; plotView.resize(500, 350); plotView.show();
    plotView.showOutput(spectrumResult(), "spectrum");
    QCoreApplication::processEvents();
    auto *plot = plotView.findChild<QCustomPlot *>();
    const auto width = plot->xAxis->range().size();
    const QPointF at(plot->axisRect()->rect().center());
    QWheelEvent event(at, plot->mapToGlobal(at.toPoint()), {}, {0, 120},
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(plot, &event);
    QVERIFY(plot->xAxis->range().size() < width);
}

void UiTests::axisSegmentsStayVisibleAndKeepBothAxes()
{
    for (const auto *theme : {"light", "dark"})
    {
        ThemeManager::instance().apply(*qApp, theme);
        PlotView view; view.showOutput(spectrumResult(), "spectrum");
        PlotStyleEditor editor(&view); editor.resize(280, 500); editor.show();
        editor.activateWindow();
        editor.findChild<QTabWidget *>("plotStyleSections")->setCurrentIndex(1);
        auto *axis = editor.findChild<QTabBar *>("styleAxis");
        QVERIFY(axis && axis->isVisible()); QCOMPARE(axis->count(), 2);
        QCOMPARE(axis->tabText(0), text("ui.x_axis"));
        QCOMPARE(axis->tabText(1), text("ui.y_axis"));
        QVERIFY(!editor.findChild<QComboBox *>("styleAxis"));
        auto *label = editor.findChild<QLineEdit *>("axisLabel");
        label->setText("Frequency X");
        QTest::mouseClick(axis, Qt::LeftButton, Qt::NoModifier, axis->tabRect(1).center());
        QCOMPARE(axis->currentIndex(), 1);
        label->setText("Power Y");
        QVERIFY(editor.apply());
        QCOMPARE(view.style().x.label, QString("Frequency X"));
        QCOMPARE(view.style().y.label, QString("Power Y"));
        axis->setFocus(Qt::TabFocusReason);
        QTest::keyClick(axis, Qt::Key_Left); QCOMPARE(axis->currentIndex(), 0);
        QCOMPARE(label->text(), QString("Frequency X"));
        QTest::keyClick(axis, Qt::Key_Right); QCOMPARE(axis->currentIndex(), 1);
        QCOMPARE(label->text(), QString("Power Y"));
        QCoreApplication::processEvents();
        const auto position = axis->mapTo(&editor, QPoint());
        QScrollArea *scroller = nullptr;
        for (auto *area : editor.findChildren<QScrollArea *>())
            if (area->isVisible()) scroller = area;
        QVERIFY(scroller); QVERIFY(scroller->verticalScrollBar()->maximum() > 0);
        scroller->verticalScrollBar()->setValue(scroller->verticalScrollBar()->maximum());
        QCoreApplication::processEvents();
        QCOMPARE(axis->mapTo(&editor, QPoint()), position);
        QVERIFY(axis->tabRect(1).right() < axis->width());
        QWheelEvent event(axis->tabRect(0).center(), axis->mapToGlobal(axis->tabRect(0).center()),
                          {}, {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(axis, &event); QCOMPARE(axis->currentIndex(), 1);
        const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
        if (!capture.isEmpty())
        {
            QDir().mkpath(capture);
            scroller->verticalScrollBar()->setValue(0);
            QTest::qWait(40);
            QVERIFY(editor.grab().save(capture + "/axis_segments_" + theme + ".png"));
        }
    }
    ThemeManager::instance().apply(*qApp, "light");
}

void UiTests::parameterRoundTrip_data()
{
    QTest::addColumn<QString>("method");
    QTest::addColumn<QString>("key");
    QTest::addColumn<QJsonValue>("value");
    for (const auto &d : makeScientificRegistry().descriptors())
    {
        if (d.key == "file_source") continue;
        for (const auto &p : d.parameters)
        {
            if (p.editorHidden) continue;
            QList<QJsonValue> values;
            if (p.kind == ParameterKind::Number)
            {
                for (double v : {p.minimum, p.maximum, 1.23456789012345, 1e-15})
                    if (v >= p.minimum && v <= p.maximum) values << QJsonValue(v);
            }
            else if (p.kind == ParameterKind::Integer)
                values = {p.minimum, p.maximum};
            else if (p.kind == ParameterKind::Boolean)
                values = {true, false};
            else if (!p.choices.isEmpty())
                for (const auto &choice : p.choices) values << QJsonValue(choice);
            else
                values = {QJsonValue(QString::fromUtf8("test data/参数.csv"))};
            int i = 0;
            for (const auto &v : values)
                QTest::newRow(qPrintable(d.key + "/" + p.key + "/" + QString::number(i++)))
                    << d.key << p.key << v;
        }
    }
}
void UiTests::parameterRoundTrip()
{
    QFETCH(QString, method); QFETCH(QString, key); QFETCH(QJsonValue, value);
    const auto registry = makeScientificRegistry();
    NodeDefinition n; n.id = QUuid::createUuid(); n.method = method;
    n.parameters = registry.normalize(method, {{key, value}}).parameters;
    ParameterEditor editor; editor.showNode(&n, registry);
    // Viewing a task, or editing another field later, must not alter any saved value.
    QCOMPARE(editor.parameters(), n.parameters);
    auto *field = editor.findChild<QWidget *>("parameter_" + key);
    QVERIFY(field);
    QVERIFY(field->toolTip().isEmpty());
    QVERIFY(editor.findChild<QLabel *>("parameterLabel_" + key)->toolTip().isEmpty());
    QVERIFY(editor.findChild<QToolButton *>("methodHelp")->toolTip().isEmpty());
    for (auto *button : editor.findChildren<QToolButton *>("ParameterHelp"))
        QVERIFY(button->toolTip().isEmpty());
    if (auto *spin = editor.findChild<QDoubleSpinBox *>("parameter_" + key))
    {
        const QString displayed = spin->text();
        bool ok = false; const double parsed = spin->locale().toDouble(displayed, &ok);
        QVERIFY2(ok, qPrintable(displayed));
        QCOMPARE(parsed, value.toDouble());
        spin->interpretText();
        QCOMPARE(editor.parameters(), n.parameters);
    }
}
void UiTests::scientificParameterTyping()
{
    const auto registry = makeScientificRegistry();
    NodeDefinition n; n.id = QUuid::createUuid(); n.method = "affine";
    n.parameters = registry.normalize(n.method, {}).parameters;
    ParameterEditor editor; editor.showNode(&n, registry); editor.show();
    auto *spin = editor.findChild<QDoubleSpinBox *>("parameter_factor"); QVERIFY(spin);
    spin->setFocus();
    for (const QString &s : {QString("1e-15"), QString("-1.23456789012345"), QString("1e50")})
    {
        spin->selectAll(); QTest::keyClicks(spin, s); QTest::keyClick(spin, Qt::Key_Tab);
        QCOMPARE(spin->value(), s.toDouble());
        QCOMPARE(editor.parameters()["factor"].toDouble(), s.toDouble());
        spin->setFocus();
    }
}

void UiTests::scientificParametersReachPreviewAndRun()
{
    QTemporaryDir directory;
    AppSettings settings; settings.databasePath = directory.filePath("parameters.sqlite");
    MainWindow window(settings, false); window.resize(1500, 950); window.show();
    const auto root = window.document()->addRoot("sine_source", {{"slope", 0}}).nodeId;
    const auto target = window.document()->addChild(root, ChildSlot::Down, "affine").nodeId;
    window.canvas()->nodeSelected(target);
    auto *editor = window.findChild<ParameterEditor *>();
    auto *factor = editor->findChild<QDoubleSpinBox *>("parameter_factor"); QVERIFY(factor);
    factor->setFocus(); factor->selectAll(); QTest::keyClicks(factor, "1e-15");
    QTest::keyClick(factor, Qt::Key_Return);
    auto *controller = window.findChild<WorkflowPreviewPanel *>()->controller();
    QTRY_COMPARE(controller->state(), WorkflowPreviewController::State::Ready);
    QVERIFY(effectiveParameters(*window.document()->node(target),root)["factor"].toDouble() == 1e-15);
    const auto before = controller->result(); QVERIFY(before);
    const auto *source = dataAs<SeriesData>(before->original.outputs["series"]);
    const auto *preview = dataAs<SeriesData>(before->output.outputs["series"]);
    QVERIFY(source && preview);
    QVERIFY(source->y[1] != 0);
    QVERIFY(preview->y[1] == source->y[1] * 1e-15);
    const auto expected = encodeData(before->output.outputs["series"]).bytes;
    // Navigating away and reopening must preserve the same stored input.
    window.canvas()->nodeSelected(root);
    window.canvas()->nodeSelected(target);
    QVERIFY(editor->parameters()["factor"].toDouble() == 1e-15);
    // Run uses the same frozen parameters as the preview, and the DB keeps them.
    QSignalSpy batches(window.runner(), &RunController::batchReady);
    window.startRun();
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy() && batches.count() == 1, 15000);
    const auto batch = qvariant_cast<std::shared_ptr<const BatchResult>>(batches.last()[0]);
    QVERIFY(batch && batch->error.isEmpty()); QCOMPARE(batch->items.size(), 1);
    QVERIFY(batch->items[0].persistence.saved());
    ResultRepository repository; QVERIFY(repository.open(settings.databasePath).isEmpty());
    const auto loaded = repository.load(batch->items[0].persistence.recordId); QVERIFY(loaded.ok());
    QCOMPARE(encodeData(loaded.run.nodes[target].outputs["series"]).bytes, expected);
    const auto workflow = workflowFromJson(loaded.run.workflowSnapshot).definition;
    for (const auto &n : workflow.nodes)
        if (n.id == target) QVERIFY(n.parameters["factor"].toDouble() == 1e-15);
}

void UiTests::maskedTableDisplay() {
    PlotView view;
    view.showOutput(NodeResult::success({{"table",makeData(TableData{{"x","y"},{{0,0},{1,-45},{2,0}},{{1,0},{1,1},{1,1}}})}}),"table");
    const auto tables=view.findChildren<QTableView*>();QVERIFY(!tables.isEmpty());
    auto* model=tables.front()->model();QVERIFY(model);
    QCOMPARE(model->data(model->index(0,1)).toString(),QString::fromUtf8("—"));
    QCOMPARE(model->data(model->index(1,1)).toString(),QString("-45"));
    QCOMPARE(model->data(model->index(2,1)).toString(),QString("0"));
}

void UiTests::outputPopupPlacement()
{
    for (const auto &bounds : {QRect(0, 0, 800, 600), QRect(-1600, -200, 900, 700),
                               QRect(20, 30, 240, 180)})
        for (const auto &point : {bounds.topLeft(), bounds.topRight(), bounds.bottomLeft(),
                                  bounds.bottomRight(), bounds.center(), QPoint(9000, -9000)})
            for (const auto &size : {QSize(360, 240), QSize(1600, 1200), QSize(40, 30)})
            {
                const auto geometry = popupGeometry(size, QRect(point, QSize(1, 1)), bounds);
                QVERIFY(bounds.contains(geometry));
                QCOMPARE(geometry.size(), size.boundedTo(bounds.size()));
            }
    const QRect bounds(0, 0, 800, 600), anchor(100, 100, 120, 40);
    QCOMPARE(popupGeometry(QSize(360, 200), anchor, bounds).topLeft(), QPoint(100, 146));
    const auto above = popupGeometry(QSize(360, 200), QRect(700, 550, 80, 40), bounds);
    QVERIFY(above.bottom() < 550);
    QCOMPARE(above.right(), bounds.right());
    QWidget owner;
    owner.resize(600, 400); owner.show();
    QMenu menu(&owner);
    menu.addAction("Fit data"); menu.addAction("Clear plot");
    const auto edge = owner.mapToGlobal(owner.rect().bottomRight());
    showMenuWithinWindow(&menu, &owner, QRect(edge, QSize(1, 1)));
    QTRY_VERIFY(menu.isVisible());
    QVERIFY(popupBounds(&owner, QRect(edge, QSize(1, 1))).contains(menu.geometry()));
    QVERIFY(menu.font().pointSizeF() >= 11);
    menu.close();
}
void UiTests::outputPickerInteraction_data()
{
    QTest::addColumn<QString>("language");
    QTest::addColumn<QString>("theme");
    for (const auto &language : {QString("zh"), QString("en")})
        for (const auto &theme : {QString("light"), QString("dark")})
            QTest::newRow(qPrintable(language + '-' + theme)) << language << theme;
}
void UiTests::outputPickerInteraction()
{
    QFETCH(QString, language); QFETCH(QString, theme);
    setLanguage(language);
    QVERIFY(ThemeManager::instance().apply(*qApp, theme).isEmpty());
    QWidget owner;
    owner.resize(680, 500); owner.show();
    const auto label = language == "zh" ? QString::fromUtf8("去趋势序列") : QString("Detrended series");
    QVector<OutputChoice> choices{{"detrended", label, true},
        {"trend", language == "zh" ? QString::fromUtf8("拟合趋势曲线") : QString("Fitted trend curve"), false},
        {"long", QString(180, QChar('X')), false}};
    auto *picker = new OutputPicker(&owner, text("ui.choose_output_from").arg("LOWESS"),
        text("ui.output_for_task").arg("MTM"), choices);
    QSignalSpy chosen(picker, &OutputPicker::chosen);
    const auto anchor = QRect(owner.mapToGlobal(owner.rect().bottomRight()), QSize(1, 1));
    picker->openAt(anchor);
    auto *list = picker->findChild<QListWidget *>("outputChoices");
    QVERIFY(list && picker->isVisible());
    QTRY_VERIFY(list->visualItemRect(list->item(0)).height() >= 36);
    QVERIFY(popupBounds(&owner, anchor).contains(picker->geometry()));
    QCOMPARE(list->item(0)->text(), label);
    QVERIFY(list->item(0)->font().bold());
    QVERIFY(!list->item(1)->font().bold());
    QVERIFY(list->item(0)->toolTip().contains(text("ui.default_output")));
    QCOMPARE(list->item(2)->toolTip(), choices[2].label);
    QVERIFY(!picker->findChild<QLineEdit *>("outputSearch")->isVisible());
    for (int row = 1; row < list->count(); ++row)
    {
        QCOMPARE(list->visualItemRect(list->item(row)).height(), list->visualItemRect(list->item(0)).height());
        QCOMPARE(list->visualItemRect(list->item(row)).width(), list->visualItemRect(list->item(0)).width());
    }
    QCOMPARE(chosen.count(), 0);
    const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!capture.isEmpty())
    {
        QDir().mkpath(capture);
        QVERIFY(picker->grab().save(capture + "/output_picker_" + language + '_' + theme + ".png"));
    }
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(1)).center());
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.front().front().toString(), QString("trend"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    setLanguage("zh");
    QVERIFY(ThemeManager::instance().apply(*qApp, "light").isEmpty());
}
void UiTests::outputPickerFilteringAndCancellation()
{
    QWidget owner; owner.resize(600, 480); owner.show();
    QVector<OutputChoice> choices;
    for (int i = 0; i < 1000; ++i)
        choices.push_back({QString::number(i), QString("Output %1").arg(i), i == 0});
    const QRect anchor(owner.mapToGlobal(QPoint(300, 200)), QSize(1, 1));
    auto *picker = new OutputPicker(&owner, "Outputs", "Input for MTM", choices);
    QSignalSpy chosen(picker, &OutputPicker::chosen);
    picker->openAt(anchor);
    auto *search = picker->findChild<QLineEdit *>("outputSearch");
    auto *list = picker->findChild<QListWidget *>("outputChoices");
    QVERIFY(search->isVisible());
    QVERIFY(list->verticalScrollBar()->maximum() > 0);
    QVERIFY(popupBounds(&owner, anchor).contains(picker->geometry()));
    const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) QVERIFY(picker->grab().save(capture + "/output_picker_search.png"));
    search->setText("not present");
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(chosen.count(), 0); QVERIFY(picker->isVisible());
    search->setText("Output 997");
    QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), QString("997"));
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.front().front().toString(), QString("997"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto *cancel = new OutputPicker(&owner, "Outputs", "Input", choices);
    QSignalSpy cancelledChoice(cancel, &OutputPicker::chosen);
    cancel->openAt(anchor);
    QTest::keyClick(cancel->findChild<QListWidget *>("outputChoices"), Qt::Key_Escape);
    QCOMPARE(cancelledChoice.count(), 0); QVERIFY(!cancel->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto *moved = new OutputPicker(&owner, "Outputs", "Input", choices);
    QSignalSpy movedChoice(moved, &OutputPicker::chosen);
    moved->openAt(anchor); owner.resize(540, 420);
    QVERIFY(!moved->isVisible()); QCOMPARE(movedChoice.count(), 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto *outside = new OutputPicker(&owner, "Outputs", "Input", choices);
    QSignalSpy outsideChoice(outside, &OutputPicker::chosen);
    QPointer<OutputPicker> outsideGuard(outside);
    outside->openAt(anchor);
    const QPoint outsidePoint(1, 1);
    QVERIFY(!outside->geometry().contains(owner.mapToGlobal(outsidePoint)));
    QTest::mouseClick(owner.windowHandle(), Qt::LeftButton, Qt::NoModifier, outsidePoint);
    QTRY_VERIFY(!outsideGuard || !outsideGuard->isVisible());
    QCOMPARE(outsideChoice.count(), 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QPointer<OutputPicker> hiddenOwner = new OutputPicker(&owner, "Outputs", "Input", choices);
    owner.hide();
    hiddenOwner->openAt(anchor); // A queued open must not outlive its hidden owner.
    QVERIFY(!hiddenOwner->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
void UiTests::outputPickerWorkflowBinding()
{
    QTemporaryDir dir; AppSettings settings;
    settings.databasePath = dir.filePath("output-picker.sqlite");
    MainWindow window(settings, false); window.resize(1300, 850); window.show();
    auto *doc = window.document();
    const auto root = doc->addRoot("sine_source").nodeId;
    const auto lowess = doc->addChild(root, ChildSlot::Down, "detrend_lowess").nodeId;
    window.canvas()->fitWorkflow();
    window.activateWindow();
    QTest::qWait(50); // Settle the deferred initial window/panel layout before opening a popup.
    const auto original = workflowToJson(doc->model().definition());
    // Follow the catalogue-menu path as well as the direct drag/drop path.
    const auto anchor = window.canvas()->viewport()->mapToGlobal(QPoint(10, 10));
    window.canvas()->slotActivated(lowess, ChildSlot::Down, anchor);
    QMenu *taskMenu = nullptr;
    for (auto *menu : window.findChildren<QMenu *>("taskPicker"))
        if (menu->isVisible()) taskMenu = menu;
    QVERIFY(taskMenu);
    auto *action = findAction(taskMenu, "mtm"); QVERIFY(action);
    // Use QMenu's real activation path, which dismisses the cascade before
    // triggering the selected action (calling QAction::trigger does not).
    auto *leaf = qobject_cast<QMenu *>(action->parent()); QVERIFY(leaf);
    QVector<QMenu *> chain;
    for (auto *menu = leaf; menu != taskMenu; menu = qobject_cast<QMenu *>(menu->parentWidget()))
    {
        QVERIFY(menu);
        chain.prepend(menu);
    }
    auto *current = taskMenu;
    for (auto *next : chain)
    {
        current->setActiveAction(next->menuAction());
        QTest::keyClick(current, Qt::Key_Right);
        QTRY_VERIFY(next->isVisible());
        current = next;
    }
    leaf->setActiveAction(action);
    QTest::keyClick(leaf, Qt::Key_Return);
    QPointer<OutputPicker> picker = window.findChild<OutputPicker *>(); QVERIFY(picker);
    QTRY_VERIFY(picker && picker->isVisible());
    auto *list = picker->findChild<QListWidget *>("outputChoices");
    QVERIFY(list && list->count() >= 2);
    QCOMPARE(workflowToJson(doc->model().definition()), original);
    int trendRow = -1;
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->data(Qt::UserRole).toString() == "trend") trendRow = row;
    QVERIFY(trendRow >= 0); list->setCurrentRow(trendRow);
    QTest::keyClick(list, Qt::Key_Return);
    QCOMPARE(doc->model().definition().nodes.size(), 3);
    const auto added = doc->model().definition().nodes.last();
    QCOMPARE(added.method, QString("mtm"));
    QCOMPARE(added.input->parent, lowess);
    QCOMPARE(added.input->output, QString("trend"));
    QVERIFY(doc->undo().ok());
    QCOMPARE(workflowToJson(doc->model().definition()), original);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    window.canvas()->methodDropped("mtm", lowess, ChildSlot::Down);
    QPointer<OutputPicker> cancel = window.findChild<OutputPicker *>(); QVERIFY(cancel);
    QTRY_VERIFY(cancel && cancel->isVisible());
    QTest::keyClick(cancel->findChild<QListWidget *>("outputChoices"), Qt::Key_Escape);
    QCOMPARE(workflowToJson(doc->model().definition()), original);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    // Deletion before the deferred popup must not resurrect it or create a stale child.
    window.canvas()->methodDropped("mtm", lowess, ChildSlot::Down);
    QPointer<OutputPicker> pending = window.findChild<OutputPicker *>(); QVERIFY(pending);
    QVERIFY(doc->removeSubtree(lowess).ok());
    QCoreApplication::processEvents();
    QVERIFY(!pending || !pending->isVisible());
    QCOMPARE(doc->model().definition().nodes.size(), 1);
}

void UiTests::scopedParameterEditAcceptsBorrowedNodeId()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("sine_source").nodeId;
    const auto child=doc.addChild(root,ChildSlot::Down,"affine").nodeId;
    // Deliberately pass an identifier stored inside the graph being replaced.
    const auto result=doc.setScopedParameters(doc.node(child)->id,root,{{"factor",3.}});
    QVERIFY2(result.ok(),qPrintable(result.error));
    QCOMPARE(result.nodeId,child);
    QCOMPARE(effectiveParameters(*doc.node(child),root)["factor"].toDouble(),3.);
    QVERIFY(doc.undo().ok());
    QCOMPARE(effectiveParameters(*doc.node(child),root)["factor"].toDouble(),1.);
}

void UiTests::scopedParametersAndMove()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("input_series").nodeId;
    const auto node=doc.addChild(root,ChildSlot::Down,"affine").nodeId;
    const auto samples=series();
    QVERIFY(doc.setSources(root,{{"A",samples,{}},{"B",samples,{}}}).isEmpty());
    const auto a=doc.datasets(node)[0].id,b=doc.datasets(node)[1].id;
    QVERIFY(doc.setScopedParameters(node,a,{{"factor",3}}).ok());
    QCOMPARE(effectiveParameters(*doc.node(node),a)["factor"].toDouble(),3.);
    QCOMPARE(effectiveParameters(*doc.node(node),b)["factor"].toDouble(),1.);
    const auto plan=doc.plan(); QVERIFY(plan.validation.ok());
    const auto encoded=workflowToJson(doc.model().definition());
    QCOMPARE(encoded["schema_version"].toInt(),2);
    const auto decoded=workflowFromJson(encoded); QVERIFY(decoded.validation.ok());
    QCOMPARE(decoded.definition.nodes[1].parameterOverrides,doc.node(node)->parameterOverrides);
    QVERIFY(doc.setScopedParameters(node,b,{{"factor",4}}).ok());
    QVERIFY(doc.setScopedParameters(node,a,{{"factor",2}},true).ok());
    QVERIFY(doc.node(node)->parameterOverrides.isEmpty());
    QCOMPARE(effectiveParameters(*doc.node(node),a)["factor"].toDouble(),2.);
    QVERIFY(doc.undo().ok());
    QCOMPARE(effectiveParameters(*doc.node(node),a)["factor"].toDouble(),3.);
    QCOMPARE(effectiveParameters(*doc.node(node),b)["factor"].toDouble(),4.);
    const auto run=WorkflowExecutor().run(plan.requests.front());
    QCOMPARE(dataAs<SeriesData>(run.nodes[node].outputs["series"])->y[0],3.); // frozen before later edits
    const auto outlier=doc.addChild(node,ChildSlot::Down,"outlier_handling").nodeId;
    const QString choice="{\"fingerprint\":\"sample\",\"points\":{\"0\":true}}";
    QVERIFY(doc.setScopedParameters(outlier,a,{{"point_overrides",choice}}).ok());
    QVERIFY(doc.setScopedParameters(outlier,b,{{"outlier_threshold",5}},true).ok());
    QCOMPARE(effectiveParameters(*doc.node(outlier),a)["point_overrides"].toString(),choice);
    QCOMPARE(effectiveParameters(*doc.node(outlier),b)["point_overrides"].toString(),QString());
    const auto sources=doc.sources().value(root);
    QVERIFY(doc.setSources(root,{sources.back()}).isEmpty());
    QVERIFY(!doc.node(outlier)->parameterOverrides.contains(a));
    QVERIFY(!doc.node(node)->parameterOverrides.contains(a));
    QVERIFY(doc.undo().ok());
    QCOMPARE(effectiveParameters(*doc.node(outlier),a)["point_overrides"].toString(),choice);
    WorkflowDocument files(makeScientificRegistry());
    auto first=files.addRoot("file_source").nodeId;
    files.setFiles(first,{"/tmp/a.csv","/tmp/b.csv"});
    const auto child=files.addChild(first,ChildSlot::Down,"affine").nodeId;
    const auto moved=files.datasets(first).front().id;
    QVERIFY(files.setScopedParameters(child,moved,{{"factor",7}}).ok());
    const auto move=files.moveDatasets(first,{}, {moved},true); QVERIFY2(move.ok(),qPrintable(move.error));
    QCOMPARE(files.files(first).size(),1); QCOMPARE(files.files(move.nodeId).size(),1);
    NodeId copy;
    for(const auto &n:files.model().definition().nodes) if(n.input && n.input->parent==move.nodeId) copy=n.id;
    QVERIFY(!copy.isNull()); QCOMPARE(effectiveParameters(*files.node(copy),moved)["factor"].toDouble(),7.);
    QVERIFY(!files.node(child)->parameterOverrides.contains(moved));
    QCOMPARE(files.datasets(move.nodeId).front().id,moved);
    QVERIFY(files.undo().ok()); QCOMPARE(files.files(first).size(),2); QVERIFY(!files.node(copy));
    QCOMPARE(effectiveParameters(*files.node(child),moved)["factor"].toDouble(),7.);
    auto target=files.addRoot("file_source").nodeId;
    auto targetChild=files.addChild(target,ChildSlot::Down,"affine",{{"factor",9}}).nodeId;
    QVERIFY(files.moveDatasets(first,target,{moved},false).ok());
    QCOMPARE(effectiveParameters(*files.node(targetChild),moved)["factor"].toDouble(),9.);
    const auto revision=files.revision();
    QVERIFY(!files.moveDatasets(first,target,{moved},false).ok()); QCOMPARE(files.revision(),revision);
}
void UiTests::movedInputKeepsNavigationAndHistory()
{
    QTemporaryDir dir; AppSettings settings; settings.databasePath=dir.filePath("moves.sqlite");
    const auto a=dir.filePath("a.csv"),b=dir.filePath("b.csv");
    for (const auto &path:{a,b}) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x,y\n0,1\n1,3\n2,2\n3,4\n");
    }
    MainWindow window(settings,false); window.resize(1364,950); window.show();
    auto *sources=window.findChild<SourcePanel*>(); sources->addFiles({a,b});
    QTRY_VERIFY(sources->readyToRun());
    auto *doc=window.document(); const auto first=sources->root();
    const auto child=doc->addChild(first,ChildSlot::Down,"affine").nodeId;
    const auto bid=doc->datasets(child)[1].id;
    sources->selectDataset(first,bid); window.canvas()->nodeSelected(child);
    QTRY_VERIFY(sources->readyToRun());
    auto *editor=window.findChild<ParameterEditor*>();
    editor->findChild<QDoubleSpinBox*>("parameter_factor")->setValue(7);
    // Open and accept immediately: the still-debounced edit must reach the copy.
    QTest::mouseClick(sources->findChild<QPushButton*>("moveSourceInputs"),Qt::LeftButton);
    auto *dialog=sources->findChild<QDialog*>("moveInputsDialog"); QVERIFY(dialog && dialog->isVisible());
    QPointer<QDialog> oldDialog=dialog;
    QTest::mouseClick(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),Qt::LeftButton);
    QTRY_VERIFY(oldDialog.isNull());
    const auto second=sources->root(); QVERIFY(second!=first);
    QCOMPARE(window.canvas()->selectedNode(),second);
    QCOMPARE(QUuid(sources->currentSource().provenance["dataset_id"].toString()),bid);
    QVERIFY(window.canvas()->viewport()->rect().contains(window.canvas()->mapFromScene(window.canvas()->nodeRect(second).center())));
    NodeId copy;
    for (const auto &n:doc->model().definition().nodes) if (n.input && n.input->parent==second) copy=n.id;
    QVERIFY(!copy.isNull()); QCOMPARE(effectiveParameters(*doc->node(copy),bid)["factor"].toDouble(),7.);
    window.canvas()->nodeSelected(copy);
    auto *undo=window.findChild<QPushButton*>("undoWorkflowEdit"), *redo=window.findChild<QPushButton*>("redoWorkflowEdit");
    QVERIFY(undo && redo); QTest::mouseClick(undo,Qt::LeftButton);
    QCOMPARE(sources->root(),first); QCOMPARE(window.canvas()->selectedNode(),first);
    QCOMPARE(QUuid(sources->currentSource().provenance["dataset_id"].toString()),bid);
    QCOMPARE(effectiveParameters(*doc->node(child),bid)["factor"].toDouble(),7.);
    QTest::mouseClick(redo,Qt::LeftButton);
    QCOMPARE(sources->root(),second); QCOMPARE(window.canvas()->selectedNode(),second);
    QCOMPARE(QUuid(sources->currentSource().provenance["dataset_id"].toString()),bid);
    QTRY_VERIFY(sources->readyToRun());
    QTest::mouseClick(sources->findChild<QPushButton*>("moveSourceInputs"),Qt::LeftButton);
    dialog=sources->findChild<QDialog*>("moveInputsDialog"); QVERIFY(dialog && dialog->isVisible());
    auto *target=dialog->findChild<QComboBox*>("moveInputsTarget"); QVERIFY(target);
    target->setCurrentIndex(target->findData(first));
    oldDialog=dialog;
    QTest::mouseClick(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),Qt::LeftButton);
    QTRY_VERIFY(oldDialog.isNull());
    QCOMPARE(window.canvas()->selectedNode(),first);
    QCOMPARE(QUuid(sources->currentSource().provenance["dataset_id"].toString()),bid); // not first existing file A
    QTest::mouseClick(undo,Qt::LeftButton);
    QCOMPARE(window.canvas()->selectedNode(),second); QCOMPARE(sources->root(),second);
    QTest::mouseClick(redo,Qt::LeftButton);
    QCOMPARE(window.canvas()->selectedNode(),first); QCOMPARE(sources->root(),first);
    auto *controller=window.findChild<WorkflowPreviewPanel*>()->controller();
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,10000);
}
void UiTests::outlierLegendUsesDisplayLabels()
{
    struct Restore { ~Restore() { setLanguage("zh"); } } restore;
    for (const auto &language:QStringList{"zh","en"}) {
        setLanguage(language);
        const auto data=series();
        const auto result=NodeResult::success({{"series",data},{"original",data},{"outliers",data}},
                                              {{"outlier_input_fingerprint","test"}});
        PlotView view; view.resize(700,500); view.showOutput(result,"series");
        const auto *plot=view.findChild<QCustomPlot*>("resultPlot"); QVERIFY(plot);
        QStringList labels;
        for (int i=0;i<plot->graphCount();++i) labels<<plot->graph(i)->name();
        QVERIFY(labels.contains(language=="zh" ? "原始序列" : "Original series"));
        QVERIFY(labels.contains(language=="zh" ? "处理后的序列" : "Processed series"));
        QVERIFY(!labels.contains("original"));
    }
}
void UiTests::scopedMetadataDoesNotCopySamples()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("input_series").nodeId;
    const auto data=series(); QVector<SourceItem> items; items.reserve(5000);
    for(int i=0;i<5000;++i) items.push_back({QString::number(i),data,{}});
    QVERIFY(doc.setSources(root,items).isEmpty());
    auto parent=root; QVector<NodeId> nodes;
    for(int i=0;i<7;++i) { parent=doc.addChild(parent,ChildSlot::Down,"affine").nodeId; nodes.push_back(parent); }
    const auto datasets=doc.datasets(root); auto draft=doc.beginEdit();
    for(auto &node:draft.state.definition.nodes) if(node.id!=root)
        for(const auto &dataset:datasets) node.parameterOverrides[dataset.id]={{"factor",2},{"offset",1}};
    auto prepared=doc.prepareEdit(std::move(draft)); QVERIFY(prepared.validation.ok());
    QVERIFY(doc.commitEdit(prepared.edit).ok());
    const auto snapshot=doc.model().freeze().snapshot;
    QVERIFY(doc.setScopedParameters(nodes.back(),datasets.front().id,{{"factor",3}}).ok());
    for(const auto &source:doc.sources()[root]) QCOMPARE(source.data,data);
    QVERIFY(doc.undo().ok());
    QCOMPARE(effectiveParameters(*doc.node(nodes.back()),datasets.front().id)["factor"].toDouble(),2.);
    const auto bytes=QJsonDocument(workflowToJson(doc.model().definition())).toJson(QJsonDocument::Compact);
    QVERIFY(bytes.size()<5*1024*1024); // metadata only; no numerical buffers or per-dataset node clones
    QElapsedTimer bulkTimer;bulkTimer.start();
    QVERIFY(doc.setScopedParameters(nodes.back(),datasets.front().id,{{"factor",4}},true).ok());
    qInfo()<<"5000-input bulk parameter edit milliseconds"<<bulkTimer.elapsed();
    QVERIFY(doc.node(nodes.back())->parameterOverrides.isEmpty());
    for(const auto &source:doc.sources()[root])QCOMPARE(source.data,data);
    QCOMPARE(effectiveParameters(*doc.node(nodes.back()),datasets.back().id)["factor"].toDouble(),4.);
    QCOMPARE(effectiveParameters(*doc.node(nodes.front()),datasets.back().id)["factor"].toDouble(),2.);
    QVERIFY(snapshot);
}
void UiTests::batchImportSkipsIncompatibleFiles()
{
    QTemporaryDir dir; const auto a=dir.filePath("a.csv"),b=dir.filePath("b.csv");
    for(const auto &item:QVector<QPair<QString,QByteArray>>{{a,"x,y,z\n0,2,4\n1,3,5\n"},{b,"x,y\n0,2\n1,3\n"}}) {
        QFile file(item.first); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(item.second);
    }
    WorkflowDocument doc(makeScientificRegistry()); SourcePanel panel(&doc); panel.addFiles({a,b}); panel.resize(400,900); panel.show();
    auto *editor=panel.findChild<FileOptionsEditor*>(); QTRY_VERIFY(editor->ready());
    auto *files=panel.findChild<QListWidget*>("sourceFiles"); files->setCurrentRow(0); files->selectAll();
    const auto oldB=doc.fileOptions(panel.root(),b);
    QSignalSpy done(&panel,&SourcePanel::batchOptionsFinished);
    auto *y=editor->findChild<QComboBox*>("fileYColumn"); y->setCurrentIndex(y->findData(2));
    QTRY_COMPARE(doc.fileOptions(panel.root(),a)["y_column"].toInt(),2);
    QVERIFY(!editor->findChild<QPushButton*>("applyFileOptions"));
    QVERIFY(!editor->findChild<QPushButton*>("applySelectedFileOptions"));
    QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
    QCOMPARE(done[0][0].toStringList().size(),1); QCOMPARE(done[0][1].toStringList().size(),1);
    QCOMPARE(doc.fileOptions(panel.root(),b),oldB);
    for(auto *message:panel.findChildren<QMessageBox*>()) message->close();
}

void UiTests::batchImportDoesNotModifyReimportedDataset()
{
    QTemporaryDir dir; const auto path=dir.filePath("input.csv");
    { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("x,y,z\n0,2,4\n1,3,5\n"); }
    WorkflowDocument doc(makeScientificRegistry()); SourcePanel panel(&doc);
    const auto second=dir.filePath("second.csv"); QVERIFY(QFile::copy(path,second));
    panel.addFiles({path,second});
    auto *editor=panel.findChild<FileOptionsEditor*>(); QTRY_VERIFY(editor->ready());
    panel.findChild<QListWidget*>("sourceFiles")->selectAll();
    const auto root=panel.root();
    const auto previousId=doc.datasets(root).first().id;
    const auto before=doc.fileOptions(root,path);
    auto desired=before; desired["y_column"]=2;
    QSignalSpy done(&panel,&SourcePanel::batchOptionsFinished);
    auto *y=editor->findChild<QComboBox*>("fileYColumn"); y->setCurrentIndex(y->findData(2));
    // Do not pump events until the same path belongs to a new dataset.
    doc.setFiles(root,{second});
    doc.setFiles(root,{path,second});
    QVERIFY(doc.datasets(root).first().id!=previousId);
    QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
    QCOMPARE(done[0][0].toStringList(),QStringList{second});
    QCOMPARE(done[0][1].toStringList().size(),1);
    QCOMPARE(doc.fileOptions(root,path),before);
    for(auto *message:panel.findChildren<QMessageBox*>()) message->close();
}

void UiTests::filePreviewPreservesFiniteValues()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("missing.csv");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("age,value\n999999,-9999\n2,NA\n3,999999\n"); f.close();
    FileOptionsEditor editor;
    editor.setFile(path, {{"missing_value_code","999999"}});
    QTRY_VERIFY(editor.ready());
    QVERIFY(!editor.findChild<QLineEdit*>("fileMissingValueCode"));
    QVERIFY(!editor.options().contains("missing_value_code"));
    const auto *table = editor.findChild<QTableWidget*>("fileRowPreview");
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->item(0,1)->text(), QString("999999"));
    QCOMPARE(table->item(0,2)->text(), QString("-9999"));
    QCOMPARE(table->item(0,1)->background().style(), Qt::NoBrush);
    QCOMPARE(table->item(2,2)->background().style(), Qt::NoBrush);
    QVERIFY(table->item(1,2)->background().style() != Qt::NoBrush);
    auto *skip = editor.findChild<QCheckBox*>("fileSkipInvalidRows");
    QVERIFY(skip);
    skip->setChecked(true);
    SeriesFileOptions options;
    QVERIFY(applyFileOptions(editor.options(), options).isEmpty());
    const auto parsed = readSeriesFile(path, options);
    QVERIFY(parsed.ok());
    QCOMPARE(parsed.report["skipped_rows"].toInt(), 1);
    QCOMPARE(dataAs<SeriesData>(parsed.data)->y, QVector<double>({-9999,999999}));
}

void UiTests::revertedImportEditLeavesOtherFilesUntouched()
{
    QTemporaryDir dir;
    const QStringList paths{dir.filePath("first.csv"), dir.filePath("second.csv")};
    for (const auto &path : paths) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x,a,b\n1,2,3\n2,4,6\n");
    }
    WorkflowDocument doc(makeScientificRegistry());
    SourcePanel panel(&doc);
    panel.resize(380, 740);
    panel.show();
    panel.addFiles(paths);
    const auto root = panel.root();
    auto before = doc.fileOptions(root, paths[1]);
    before["y_column"] = 2;
    before["x_unit"] = "Ma";
    QVERIFY(doc.setFileOptions(root, {paths[1]}, before));
    auto *list = panel.findChild<QListWidget *>("sourceFiles");
    list->setCurrentRow(0);
    list->setFocus();
    QTest::keySequence(list, QKeySequence::SelectAll);
    QCOMPARE(list->selectedItems().size(), 2);
    QTest::mouseClick(panel.findChild<QToolButton *>("sourceOptionsToggle"), Qt::LeftButton);
    auto *form = panel.findChild<FileOptionsEditor *>();
    QTRY_VERIFY(form->ready());
    auto *unit = form->findChild<QLineEdit *>("fileXUnit");
    QSignalSpy commits(&panel, &SourcePanel::batchOptionsFinished);
    QTest::mouseClick(unit, Qt::LeftButton);
    QTest::keyClicks(unit, "x");
    QTest::keyClick(unit, Qt::Key_Backspace);
    QTest::keyClick(unit, Qt::Key_Tab);
    QTRY_VERIFY_WITH_TIMEOUT(panel.readyToRun(), 5000);
    QCOMPARE(doc.fileOptions(root, paths[1]), before);
    QCOMPARE(commits.size(), 0);
    const auto revision = doc.revision();
    QVERIFY(panel.applyCurrentOptions());
    QCOMPARE(doc.revision(), revision);
}
void UiTests::reimportingAPathStartsWithANewStatus()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("sample.csv");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x,y\n0,1\n1,2\n");
    file.close();
    WorkflowDocument doc(makeScientificRegistry());
    SourcePanel panel(&doc);
    panel.addFiles({path});
    const auto root = panel.root();
    const auto originalId = doc.datasets(root).front().id;
    auto *list = panel.findChild<QListWidget *>("sourceFiles");
    panel.setFileState(root, path, SourceFileState::Failed);
    QCOMPARE(list->item(0)->data(Qt::UserRole+2).toInt(), int(SourceFileState::Failed));
    doc.setFiles(root, {});
    QCOMPARE(list->count(), 0);
    panel.addFiles({path});
    QVERIFY(doc.datasets(root).front().id != originalId);
    QCOMPARE(list->item(0)->data(Qt::UserRole+2).toInt(), int(SourceFileState::Pending));
    QVERIFY(doc.undo().ok());
    QVERIFY(doc.undo().ok());
    QCOMPARE(doc.datasets(root).front().id, originalId);
    QCOMPARE(list->item(0)->data(Qt::UserRole+2).toInt(), int(SourceFileState::Failed));
}
void UiTests::batchImportCapturesSelectionAndLatestEdit()
{
    QTemporaryDir dir; QStringList paths;
    for (int i=0;i<3;++i) { const auto path=dir.filePath(QString::number(i)+".csv"); paths<<path;
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x,y\n0,2\n1,3\n"); }
    WorkflowDocument doc(makeScientificRegistry()); SourcePanel panel(&doc); panel.addFiles(paths);
    auto *editor=panel.findChild<FileOptionsEditor*>(); QTRY_VERIFY(editor->ready());
    auto *files=panel.findChild<QListWidget*>("sourceFiles"); files->setCurrentRow(0); files->item(1)->setSelected(true);
    const auto root=panel.root(); const auto node=doc.addChild(root,ChildSlot::Down,"affine").nodeId;
    const auto nodeParameters=doc.node(node)->parameters;
    const auto untouched=doc.fileOptions(root,paths[2]);
    auto *unit=editor->findChild<QLineEdit*>("fileXUnit"); QVERIFY(unit);
    QSignalSpy done(&panel,&SourcePanel::batchOptionsFinished);
    unit->setText("ka"); editor->flushEdits();
    unit->setText("yr"); editor->flushEdits(); // queued while the first validation runs
    unit->setText("Ma"); // capture A/B before the delayed edit is flushed by selecting C
    editor->findChild<QLineEdit*>("fileYUnit")->setText("counts");
    files->setCurrentRow(2,QItemSelectionModel::ClearAndSelect);
    QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,10000);
    QTRY_VERIFY(panel.readyToRun());
    QCOMPARE(doc.fileOptions(root,paths[0])["x_unit"].toString(),QString("Ma"));
    QCOMPARE(doc.fileOptions(root,paths[1])["x_unit"].toString(),QString("Ma"));
    QCOMPARE(doc.fileOptions(root,paths[0])["y_unit"].toString(),QString("counts"));
    QCOMPARE(doc.fileOptions(root,paths[1])["y_unit"].toString(),QString("counts"));
    QCOMPARE(doc.fileOptions(root,paths[2]),untouched);
    QCOMPARE(doc.node(node)->parameters,nodeParameters);
    for (const auto &event:done) QVERIFY(event[1].toStringList().isEmpty());
    auto *help=panel.findChild<QToolButton*>("sourceSelectionHelp"); QVERIFY(help);
    QTest::mouseClick(help,Qt::LeftButton);
    auto *dialog=panel.findChild<HelpDialog*>(); QVERIFY(dialog);
    QCOMPARE(dialog->currentTopic().anchor,QString("input-selection")); dialog->close();
}
void UiTests::batchImportKeyboardMouseScope()
{
    QTemporaryDir dir; QStringList paths;
    QVERIFY(QDir(dir.path()).mkpath("A")); QVERIFY(QDir(dir.path()).mkpath("B"));
    for (const auto &name:QStringList{"A/same.txt","B/same.txt","third.txt","fourth.txt"}) {
        const auto path=dir.filePath(name); paths<<path; QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x y\n0 1\n1 2\n2 3\n");
    }
    WorkflowDocument doc(makeScientificRegistry()); SourcePanel panel(&doc);
    panel.resize(300,800); panel.show(); panel.addFiles(paths);
    auto *editor=panel.findChild<FileOptionsEditor*>(); QTRY_VERIFY(editor->ready());
    const auto root=panel.root(); const auto node=doc.addChild(root,ChildSlot::Down,"affine").nodeId;
    const auto nodeParameters=doc.node(node)->parameters;
    const auto untouched=doc.fileOptions(root,paths[2]);
    auto *list=panel.findChild<QListWidget*>("sourceFiles");
    const auto click=[&](int row,Qt::KeyboardModifiers mods=Qt::NoModifier) {
        QTest::mouseClick(list->viewport(),Qt::LeftButton,mods,list->visualItemRect(list->item(row)).center());
    };
    click(0); QTest::keyClick(list,Qt::Key_A,Qt::ControlModifier);
    QCOMPARE(list->selectedItems().size(),4);
    click(0,Qt::ControlModifier); QVERIFY(!list->item(0)->isSelected());
    QVERIFY(!editor->isEnabled());
    click(0); click(1,Qt::ShiftModifier); QCOMPARE(list->selectedItems().size(),2);
    QTRY_VERIFY(editor->ready()); QVERIFY(editor->isEnabled());
    auto *unit=editor->findChild<QLineEdit*>("fileXUnit");
    QTest::mouseClick(unit,Qt::LeftButton); QTest::keyClick(unit,Qt::Key_A,Qt::ControlModifier);
    QSignalSpy done(&panel,&SourcePanel::batchOptionsFinished);
    QTest::keyClicks(unit,"kyr"); click(2); // Focus loss + selection change, before debounce.
    QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000); QTRY_VERIFY(panel.readyToRun());
    QCOMPARE(done[0][0].toStringList().size(),2); QVERIFY(done[0][1].toStringList().isEmpty());
    for(int row=0;row<2;row++)QCOMPARE(doc.fileOptions(root,paths[row])["x_unit"].toString(),QString("kyr"));
    QCOMPARE(doc.fileOptions(root,paths[2]),untouched); QCOMPARE(doc.node(node)->parameters,nodeParameters);
    QVERIFY(doc.undo().ok());
    for(int row=0;row<2;row++)QCOMPARE(doc.fileOptions(root,paths[row])["x_unit"].toString(),QString());
    QVERIFY(doc.redo().ok());
    for(int row=0;row<2;row++)QCOMPARE(doc.fileOptions(root,paths[row])["x_unit"].toString(),QString("kyr"));
    QTRY_VERIFY(panel.readyToRun());
    click(0);QTest::keyClick(list,Qt::Key_A,Qt::ControlModifier);
    QTest::mouseClick(panel.findChild<QPushButton*>("removeSourceInput"),Qt::LeftButton);
    QVERIFY(doc.files(root).isEmpty());QVERIFY(doc.undo().ok());QCOMPARE(doc.files(root),paths);
}
void UiTests::batchImportPreservesConcurrentOptions()
{
    QTemporaryDir dir; QStringList paths;
    for(int i=0;i<2;i++){const auto path=dir.filePath(QString::number(i)+".txt");paths<<path;
        QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write("x y\n0 1\n1 2\n2 3\n");}
    WorkflowDocument doc(makeScientificRegistry()); SourcePanel panel(&doc);panel.addFiles(paths);
    auto *editor=panel.findChild<FileOptionsEditor*>();QTRY_VERIFY(editor->ready());
    auto *list=panel.findChild<QListWidget*>("sourceFiles");list->selectAll();
    const auto root=panel.root();QSignalSpy done(&panel,&SourcePanel::batchOptionsFinished);
    editor->findChild<QLineEdit*>("fileXUnit")->setText("kyr");editor->flushEdits();
    auto external=doc.fileOptions(root,paths[1]);external["x_unit"]="external";
    QVERIFY(doc.setFileOptions(root,{paths[1]},external)); // Before queued worker completion is delivered.
    QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);
    QCOMPARE(done[0][0].toStringList(),QStringList{paths[0]});QCOMPARE(done[0][1].toStringList().size(),1);
    QCOMPARE(doc.fileOptions(root,paths[0])["x_unit"].toString(),QString("kyr"));
    QCOMPARE(doc.fileOptions(root,paths[1])["x_unit"].toString(),QString("external"));
    for(auto *message:panel.findChildren<QMessageBox*>())message->close();
}
void UiTests::sourceActionsFitNarrowPanel()
{
    const auto savedFont = qApp->font();
    struct Restore { QFont font; ~Restore(){setLanguage("zh");qApp->setFont(font);} } restore{savedFont};
    for(const auto &language:QStringList{"zh","en"}) for(int points:{10,14}) {
        setLanguage(language);
        auto font=savedFont; font.setPointSize(points); qApp->setFont(font);
        WorkflowDocument doc(makeScientificRegistry()); SourcePanel panel(&doc);
        panel.show();
        for(int width:{220,270,340}) {
            panel.resize(width,700); QTest::qWait(20);
            QCOMPARE(panel.width(),width);
            for(const auto &name:QStringList{"moveSourceInputs","removeSourceInput","sourceSelectionHelp"}) {
                const auto *button=panel.findChild<QAbstractButton*>(name);QVERIFY(button);
                const auto context=QString("%1 %2pt width=%3 %4 size=%5x%6 min=%7x%8")
                    .arg(language).arg(points).arg(width).arg(name).arg(button->width()).arg(button->height())
                    .arg(button->minimumSizeHint().width()).arg(button->minimumSizeHint().height());
                QVERIFY2(button->width()>=button->minimumSizeHint().width(),qPrintable(context));
                if(button->hasHeightForWidth())
                    QTRY_VERIFY2(button->height()>=button->heightForWidth(button->width()),qPrintable(context));
                const auto position=button->mapTo(&panel,QPoint(0,0));
                QVERIFY2(position.x()>=0 && position.x()+button->width()<=panel.width(),qPrintable(context));
            }
        }
        auto *toggle = panel.findChild<QToolButton *>("sourceOptionsToggle");
        auto *options = panel.findChild<FileOptionsEditor *>();
        auto *split = panel.findChild<QSplitter *>("sourceOptionsSplitter");
        auto *list = panel.findChild<QListWidget *>("sourceFiles");
        QVERIFY(toggle && options && split && list);
        QVERIFY(!toggle->isChecked() && options->isHidden());
        const auto fullHeight = list->height();
        toggle->click(); QTRY_VERIFY(options->isVisible() && list->height() < fullHeight);
        split->setSizes({400, 225});
        const int chosenHeight = split->sizes().at(1);
        toggle->click(); QTRY_COMPARE(list->height(),fullHeight);
        toggle->click(); QTRY_COMPARE(split->sizes().at(1),chosenHeight);
        toggle->click();
        // Force the two action labels to exceed the available row width even
        // on hosts with compact fonts, then verify wrapping and recovery.
        auto *move=panel.findChild<QAbstractButton*>("moveSourceInputs");
        auto *remove=panel.findChild<QAbstractButton*>("removeSourceInput");
        move->setMinimumWidth(110); remove->setMinimumWidth(110);
        panel.resize(220,700);
        QTRY_VERIFY(remove->mapTo(&panel,QPoint()).y() >=
                    move->mapTo(&panel,QPoint(0,move->height())).y());
        QCOMPARE(panel.width(),220);
        QVERIFY(move->width()>=110 && remove->width()>=110);
        panel.resize(500,700);
        QTRY_VERIFY(remove->mapTo(&panel,QPoint()).y() <
                    move->mapTo(&panel,QPoint(0,move->height())).y());
    }
}
void UiTests::evolutionPresetIsEditable()
{
    auto registry=makeScientificRegistry();
    NodeDefinition node; node.id=QUuid::createUuid(); node.method="evolution_fft";
    ParameterEditor editor; editor.showNode(&node,registry); editor.show();
    auto *preset=editor.findChild<QPushButton*>("resetNodeDefaults"); QVERIFY(preset);
    QVERIFY(!editor.dirty());
    QCOMPARE(editor.parameters()["evo_window_mode"].toString(),QString("span_percent"));
    QCOMPARE(editor.parameters()["evo_step_mode"].toString(),QString("target_windows"));
    QCOMPARE(editor.parameters()["evo_target_windows"].toInt(),500);
    QVERIFY(editor.findChild<QWidget*>("parameterRow_evo_overlap_percent")->isHidden());
    QVERIFY(editor.findChild<QWidget*>("parameterRow_evo_step")->isHidden());
    QVERIFY(!editor.findChild<QWidget*>("parameterRow_evo_target_windows")->isHidden());
    QCOMPARE(editor.parameters()["evo_padding"].toInt(),4);
    QCOMPARE(editor.parameters()["evo_taper"].toString(),QString("rectangular"));
    QVERIFY(editor.parameters()["evo_normalize"].toBool()); QVERIFY(editor.parameters()["evo_amplitude"].toBool());
    QVERIFY(editor.findChild<QWidget*>("parameterRow_evo_window")->isHidden());
    QVERIFY(!editor.findChild<QWidget*>("parameterRow_evo_window_percent")->isHidden());
    auto *percent=editor.findChild<QDoubleSpinBox*>("parameter_evo_window_percent"); percent->setValue(30);
    QCOMPARE(editor.parameters()["evo_window_percent"].toDouble(),30.);
    auto *target=editor.findChild<QSpinBox*>("parameter_evo_target_windows"); QVERIFY(target);
    target->setValue(250); QCOMPARE(editor.parameters()["evo_target_windows"].toInt(),250);
    auto *mode=editor.findChild<QComboBox*>("parameter_evo_step_mode"); QVERIFY(mode);
    mode->setCurrentIndex(mode->findData("overlap"));
    QVERIFY(!editor.findChild<QWidget*>("parameterRow_evo_overlap_percent")->isHidden());
    QVERIFY(editor.findChild<QWidget*>("parameterRow_evo_target_windows")->isHidden());
    mode->setCurrentIndex(mode->findData("target_windows"));
    QCOMPARE(target->value(),250);
    // Saving/reopening preserves the explicit mode and edited target.
    node.parameters=editor.parameters(); editor.showNode(&node,registry);
    QCOMPARE(editor.parameters()["evo_target_windows"].toInt(),250);
    QCOMPARE(editor.parameters()["evo_step_mode"].toString(),QString("target_windows"));
    QVERIFY(!editor.dirty());
    preset=editor.findChild<QPushButton*>("resetNodeDefaults");
    QTest::mouseClick(preset,Qt::LeftButton);
    QVERIFY(editor.dirty());
    QCOMPARE(editor.parameters(),registry.normalize("evolution_fft",{}).parameters);
    // Old fixed-count input must not be reinterpreted just by opening the panel.
    node.parameters={{"evo_window",80},{"evo_step",5},{"evo_nfft",160}};
    editor.showNode(&node,registry);
    QVERIFY(!editor.dirty());
    QCOMPARE(editor.parameters()["evo_window_mode"].toString(),QString("samples"));
    QCOMPARE(editor.parameters()["evo_step_mode"].toString(),QString("samples"));
    QCOMPARE(editor.parameters()["evo_fft_mode"].toString(),QString("length"));
    // Rebuilding a focused form must not commit or edit the replacement node.
    auto *frequency=editor.findChild<QDoubleSpinBox*>("parameter_frequency_max");
    frequency->setFocus(); frequency->setProperty("frequencyZeroEdited",true);
    QSignalSpy rebuildingEdits(&editor,&ParameterEditor::edited);
    // Reset does not erase decisions belonging to one specific input.
    node.method="outlier_handling";
    node.parameters={{"point_overrides",QString("{\"fingerprint\":\"sample\",\"points\":{\"0\":true}}")}};
    editor.showNode(&node,registry);
    QCOMPARE(rebuildingEdits.size(),0); QVERIFY(!editor.dirty());
    const auto points=editor.parameters().value("point_overrides");
    QTest::mouseClick(editor.findChild<QPushButton*>("resetNodeDefaults"),Qt::LeftButton);
    QCOMPARE(editor.parameters()["point_overrides"],points);
}


void UiTests::batchPreservesInheritedDecisions()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("input_series").nodeId;
    const QString choice="{\"fingerprint\":\"sample\",\"points\":{\"0\":true}}";
    // Restored single-run parameters can carry point decisions in the base.
    const auto node=doc.addChild(root,ChildSlot::Down,"outlier_handling",{{"point_overrides",choice}}).nodeId;
    QVERIFY(doc.setSources(root,{{"A",series(),{}},{"B",series(),{}}}).isEmpty());
    const auto a=doc.datasets(node)[0].id,b=doc.datasets(node)[1].id;
    auto bparams=doc.node(node)->parameters;bparams["point_overrides"]="";
    QVERIFY(doc.setScopedParameters(node,b,bparams).ok());
    auto p=effectiveParameters(*doc.node(node),a);p["outlier_threshold"]=5;
    const auto before=workflowToJson(doc.model().definition());
    QVERIFY(doc.setScopedParameters(node,a,p,true).ok());
    QCOMPARE(effectiveParameters(*doc.node(node),a)["point_overrides"].toString(),choice);
    QCOMPARE(effectiveParameters(*doc.node(node),b)["point_overrides"].toString(),QString());
    QCOMPARE(doc.node(node)->parameters["point_overrides"].toString(),QString());
    QVERIFY(doc.undo().ok());QCOMPARE(workflowToJson(doc.model().definition()),before);
    QVERIFY(doc.redo().ok());
    auto sources=doc.sources().value(root);sources.push_back({"C",series(),{}});
    QVERIFY(doc.setSources(root,sources).isEmpty());
    const auto c=doc.datasets(node)[2].id;
    QCOMPARE(effectiveParameters(*doc.node(node),c)["point_overrides"].toString(),QString());
    QCOMPARE(effectiveParameters(*doc.node(node),c)["outlier_threshold"].toDouble(),5.);
    const auto decoded=workflowFromJson(workflowToJson(doc.model().definition()));QVERIFY(decoded.validation.ok());
    WorkflowDocument reopened(makeScientificRegistry());QVERIFY(reopened.replaceDefinition(decoded.definition).ok());
    QVERIFY(reopened.setSources(root,doc.sources().value(root)).isEmpty());
    QCOMPARE(effectiveParameters(*reopened.node(node),a)["point_overrides"].toString(),choice);
    QCOMPARE(effectiveParameters(*reopened.node(node),b)["point_overrides"].toString(),QString());
    // Broadcasting from B also preserves A's decisions; neither supplies C's points.
    QVERIFY(reopened.setScopedParameters(node,b,effectiveParameters(*reopened.node(node),b),true).ok());
    QCOMPARE(effectiveParameters(*reopened.node(node),a)["point_overrides"].toString(),choice);
}
void UiTests::batchRejectsStaleScope()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("input_series").nodeId;
    const auto other=doc.addRoot("sine_source").nodeId;
    const auto node=doc.addChild(root,ChildSlot::Down,"evolution_fft").nodeId;
    QVERIFY(doc.setSources(root,{{"A",series(),{}},{"B",series(),{}}}).isEmpty());
    const auto stale=doc.datasets(node)[0].id;
    const auto sources=doc.sources().value(root);QVERIFY(doc.setSources(root,{sources.back()}).isEmpty());
    const auto before=workflowToJson(doc.model().definition());const auto revision=doc.revision();
    for(const auto &scope:QVector<QUuid>{stale,other,QUuid{}}){
        QVERIFY(!doc.setScopedParameters(node,scope,{{"evo_window_percent",30}},true).ok());
        QCOMPARE(doc.revision(),revision);QCOMPARE(workflowToJson(doc.model().definition()),before);
    }
    const auto active=doc.datasets(node)[0].id;doc.setEditingLocked(true);
    const auto lockedRevision=doc.revision();
    QVERIFY(!doc.setScopedParameters(node,active,{{"evo_window_percent",30}},true).ok());
    QCOMPARE(doc.revision(),lockedRevision);doc.setEditingLocked(false);
    QVERIFY(doc.setScopedParameters(node,active,{{"evo_window_percent",30}},true).ok());
}
void UiTests::automaticScopeReplayIsolation()
{
    WorkflowDocument doc(makeScientificRegistry());
    auto input=[](int count,double step){SeriesData s;for(int i=0;i<count;++i){s.x<<i*step;s.y<<std::sin(i*.13);}return makeData(s);};
    const auto root=doc.addRoot("input_series").nodeId;
    const auto otherRoot=doc.addRoot("input_series").nodeId;
    const auto node=doc.addChild(root,ChildSlot::Down,"evolution_fft").nodeId;
    const auto sibling=doc.addChild(root,ChildSlot::Right,"evolution_fft").nodeId;
    const auto other=doc.addChild(otherRoot,ChildSlot::Down,"evolution_fft").nodeId;
    QVERIFY(doc.setSources(root,{{"same.csv",input(501,.25),{}},{"same.csv",input(1001,.5),{}}}).isEmpty());
    QVERIFY(doc.setSources(otherRoot,{{"same.csv",input(801,1.),{}}}).isEmpty());
    const auto a=doc.datasets(node)[0].id,b=doc.datasets(node)[1].id;
    const auto initial=doc.plan();QVERIFY(initial.validation.ok());
    auto p=doc.node(node)->parameters;p["evo_window_percent"]=30;
    QVERIFY(doc.setScopedParameters(node,a,p,true).ok());
    QCOMPARE(doc.node(sibling)->parameters["evo_window_percent"].toDouble(),20.);
    QCOMPARE(doc.node(other)->parameters["evo_window_percent"].toDouble(),20.);
    auto manual=p;manual["evo_window_mode"]="samples";manual["evo_window"]=80;
    QVERIFY(doc.setScopedParameters(node,b,manual).ok());
    // Reordering does not exchange the two same-named datasets' settings.
    const auto sources=doc.sources().value(root);QVERIFY(doc.setSources(root,{sources[1],sources[0]}).isEmpty());
    QCOMPARE(effectiveParameters(*doc.node(node),a)["evo_window_mode"].toString(),QString("span_percent"));
    QCOMPARE(effectiveParameters(*doc.node(node),b)["evo_window_mode"].toString(),QString("samples"));
    const auto encoded=workflowToJson(doc.model().definition());const auto decoded=workflowFromJson(encoded);QVERIFY(decoded.validation.ok());
    WorkflowDocument reopened(makeScientificRegistry());QVERIFY(reopened.replaceDefinition(decoded.definition).ok());
    for(auto it=doc.sources().cbegin();it!=doc.sources().cend();++it)QVERIFY(reopened.setSources(it.key(),it.value()).isEmpty());
    QCOMPARE(workflowToJson(reopened.model().definition()),encoded);
    const auto plan=reopened.plan();QVERIFY(plan.validation.ok());QCOMPARE(plan.requests.size(),3);
    int seen=0;
    for(const auto &request:plan.requests){
        const auto run=WorkflowExecutor().run(request);if(!run.nodes.contains(node))continue;
        const auto result=run.nodes[node];QCOMPARE(result.state,NodeState::Succeeded);
        // Restored order is B then A, one manual and one input-relative setting.
        QCOMPARE(result.report["window_samples"].toInt(),seen==0?80:150);++seen;
    }
    QCOMPARE(seen,2);
    // A frozen request retains its pre-broadcast settings even after later edits.
    for(const auto &request:initial.requests){const auto run=WorkflowExecutor().run(request);if(run.nodes.contains(node)){
        const auto count=run.nodes[node].report["window_samples"].toInt();QVERIFY(count==100||count==200);
    }}
}


void UiTests::automaticBatchFailureIsolation()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("input_series").nodeId;
    const auto node=doc.addChild(root,ChildSlot::Down,"evolution_fft").nodeId;
    auto input=[](int count){SeriesData s;for(int i=0;i<count;++i){s.x<<double(i);s.y<<std::sin(i*.13);}return makeData(s);};
    QVERIFY(doc.setSources(root,{{"short",input(21),{}},{"long",input(501),{}}}).isEmpty());
    const auto active=doc.datasets(node)[1].id;
    QVERIFY(doc.setScopedParameters(node,active,doc.node(node)->parameters,true).ok());
    const auto before=workflowToJson(doc.model().definition());const auto plan=doc.plan();QVERIFY(plan.validation.ok());
    QTemporaryDir dir;const auto path=dir.filePath("failure-isolation.sqlite");
    const auto results=BatchRunner().run(plan,path,2);QVERIFY(results.error.isEmpty());QCOMPARE(results.items.size(),2);
    ResultRepository repo;QVERIFY(repo.open(path,false).isEmpty());int success=0,failure=0;
    for(const auto &item:results.items){
        QVERIFY(item.persistence.saved());const auto saved=repo.load(item.persistence.recordId);QVERIFY(saved.ok());
        const auto result=saved.run.nodes[node];
        if(item.sourceLabel=="short"){QCOMPARE(result.state,NodeState::Failed);++failure;}
        else {QCOMPARE(result.state,NodeState::Succeeded);QCOMPARE(result.report["window_samples"].toInt(),100);++success;}
    }
    QCOMPARE(success,1);QCOMPARE(failure,1);
    QCOMPARE(workflowToJson(doc.model().definition()),before); // No runtime writeback of resolved numbers.
}



void UiTests::duplicateSourceSelectionUsesIdentity()
{
    WorkflowDocument doc(makeScientificRegistry());const auto root=doc.addRoot("input_series").nodeId;
    QVERIFY(doc.setSources(root,{{"same output",series(),{}},{"same output",series(),{}}}).isEmpty());
    SourcePanel panel(&doc);panel.show();panel.selectRoot(root);
    const auto a=doc.datasets(root)[0].id;panel.selectDataset(root,a);
    QCOMPARE(QUuid(panel.currentSource().provenance["dataset_id"].toString()),a);
    panel.refresh();QCOMPARE(QUuid(panel.currentSource().provenance["dataset_id"].toString()),a);
    auto sources=doc.sources().value(root);std::reverse(sources.begin(),sources.end());
    QVERIFY(doc.setSources(root,sources).isEmpty());
    QCOMPARE(QUuid(panel.currentSource().provenance["dataset_id"].toString()),a);
    sources.push_back({"same output",series(),{}});QVERIFY(doc.setSources(root,sources).isEmpty());
    QCOMPARE(QUuid(panel.currentSource().provenance["dataset_id"].toString()),a);
}

void UiTests::removeInputFlushesPendingParameters()
{
    QTemporaryDir dir;AppSettings settings;settings.databasePath=dir.filePath("remove-pending.sqlite");
    QStringList paths;for(int k=0;k<2;++k){const auto path=dir.filePath(QString::number(k)+".csv");paths<<path;
        QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write("x,y\n0,1\n1,2\n2,3\n");}
    MainWindow window(settings,false);window.resize(1500,950);window.show();
    auto *sources=window.findChild<SourcePanel*>();sources->addFiles(paths);QTRY_VERIFY(sources->readyToRun());
    auto *doc=window.document();const auto root=sources->root();
    const auto node=doc->addChild(root,ChildSlot::Down,"affine").nodeId;
    auto *files=sources->findChild<QListWidget*>("sourceFiles");files->setCurrentRow(0,QItemSelectionModel::ClearAndSelect);
    const auto a=doc->datasets(node)[0].id,b=doc->datasets(node)[1].id;
    window.canvas()->nodeSelected(node);auto *editor=window.findChild<ParameterEditor*>();
    auto *factor=editor->findChild<QDoubleSpinBox*>("parameter_factor");QVERIFY(factor);
    factor->setFocus();QTest::keyClick(factor,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(factor,"7");
    QVERIFY(editor->dirty());
    QTest::mouseClick(sources->findChild<QPushButton*>("removeSourceInput"),Qt::LeftButton);
    QCOMPARE(doc->datasets(node).size(),1);
    QVERIFY(!editor->dirty()); // The deleted input must not leave an uncommittable form.
    QCOMPARE(editor->parameters()["factor"].toDouble(),1.);
    QCOMPARE(effectiveParameters(*doc->node(node),b)["factor"].toDouble(),1.);
    QTest::mouseClick(window.findChild<QPushButton*>("undoWorkflowEdit"),Qt::LeftButton);
    QCOMPARE(doc->datasets(node).size(),2);
    QCOMPARE(effectiveParameters(*doc->node(node),a)["factor"].toDouble(),7.);
    QTest::mouseClick(window.findChild<QPushButton*>("redoWorkflowEdit"),Qt::LeftButton);
    QCOMPARE(doc->datasets(node).size(),1);
    // A surviving scope stays editable after the old draft timer would have fired.
    QTest::qWait(300);factor=editor->findChild<QDoubleSpinBox*>("parameter_factor");QVERIFY(factor);
    factor->setValue(3);QTRY_COMPARE(effectiveParameters(*doc->node(node),b)["factor"].toDouble(),3.);
}
void UiTests::pairManagerFlushesPendingParameters()
{
    QTemporaryDir dir;AppSettings settings;settings.databasePath=dir.filePath("pair-pending.sqlite");
    MainWindow window(settings,false);window.resize(1500,950);window.show();auto *doc=window.document();
    const auto root=doc->addRoot("input_series").nodeId;
    QVERIFY(doc->setSources(root,{{"A",series(),{}},{"B",series(),{}}}).isEmpty());
    const auto inputs=doc->datasets(root);
    const auto joint=doc->addChild(root,ChildSlot::Down,"cross_spectrum",{{"psd_window",64},{"psd_overlap",32}}).nodeId;
    const auto pair=QUuid::createUuid();
    QVERIFY(doc->setPairs(joint,{{pair,"self",{inputs[0].id,"series"},{inputs[1].id,"series"}}}).isEmpty());
    window.canvas()->nodeSelected(joint);auto *editor=window.findChild<ParameterEditor*>();
    auto *length=editor->findChild<QSpinBox*>("parameter_psd_window");QVERIFY(length);length->setValue(96);QVERIFY(editor->dirty());
    auto *manage=window.findChild<QPushButton*>("managePairs");QVERIFY(manage&&manage->isVisible());
    QTest::mouseClick(manage,Qt::LeftButton);
    QPointer<PairingDialog> dialog=window.findChild<PairingDialog*>();QVERIFY(dialog);
    QTest::qWait(350);QVERIFY(dialog&&dialog->isVisible());
    QCOMPARE(effectiveParameters(*doc->node(joint),pair)["psd_window"].toInt(),96);
    dialog->reject();QTRY_VERIFY(!dialog||!dialog->isVisible());
}
void UiTests::helpSearchDiscardsStaleResults()
{
    setLanguage("zh");HelpDialog dialog;dialog.show();QVERIFY(dialog.navigate(userGuideTopic()));
    auto *search=dialog.findChild<QLineEdit*>("helpGlobalSearch");auto *results=dialog.findChild<QListWidget*>("helpSearchResults");
    search->setText("evo_target_windows");QTRY_VERIFY(results->count()>0);
    const auto topic=dialog.currentTopic();search->setText("__absent_new_query__");
    // No old hits may remain actionable during the debounce interval.
    QCOMPARE(results->count(),0);
    QTest::keyClick(search,Qt::Key_Return);QCOMPARE(dialog.currentTopic().path,topic.path);QCOMPARE(dialog.currentTopic().anchor,topic.anchor);
}
void UiTests::helpColdSearchEnterOpensResult()
{
    setLanguage("zh");HelpDialog dialog;dialog.show();QVERIFY(dialog.navigate(userGuideTopic()));
    auto *search=dialog.findChild<QLineEdit*>("helpGlobalSearch");search->setText("evo_target_windows");
    QTest::keyClick(search,Qt::Key_Return);
    QTRY_VERIFY(dialog.currentTopic().anchor.contains("evo_target_windows"));
    // Pending Enter is cancelled by new text, clear, or a different navigation.
    for(int cancelMode=0;cancelMode<3;++cancelMode){
        HelpDialog pending;pending.show();QVERIFY(pending.navigate(userGuideTopic()));
        auto *query=pending.findChild<QLineEdit*>("helpGlobalSearch");
        query->setText("evo_target_windows");QTest::keyClick(query,Qt::Key_Return);
        if(cancelMode==0)query->setText("__missing_after_enter__");
        else if(cancelMode==1)query->clear();
        else QVERIFY(pending.navigate({"FAQ.md",{}}));
        const auto expected=pending.currentTopic();
        if(cancelMode==0)QTRY_COMPARE(pending.findChild<QLabel*>("helpSearchStatus")->text(),text("help.no_results"));
        else if(cancelMode==2)QTRY_VERIFY(pending.findChild<QListWidget*>("helpSearchResults")->count()>0);
        else QTest::qWait(300);
        QCOMPARE(pending.currentTopic().path,expected.path);QCOMPARE(pending.currentTopic().anchor,expected.anchor);
    }
}

void UiTests::automaticEvolutionScopes()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("input_series").nodeId;
    const auto evo=doc.addChild(root,ChildSlot::Down,"evolution_fft").nodeId;
    auto input=[](int count,double dt) {
        SeriesData s; s.xAxis={"Age","ka"}; s.yAxis={"Signal","u"};
        for(int i=0;i<count;++i) {s.x.push_back(i*dt);s.y.push_back(std::sin(i*.13)+.2*std::cos(i*.47));}
        return makeData(s);
    };
    QVERIFY(doc.setSources(root,{{"A",input(501,.25),{}},{"B",input(1201,.5),{}}}).isEmpty());
    const auto a=doc.datasets(evo)[0].id,b=doc.datasets(evo)[1].id;
    const auto defaults=doc.model().registry().normalize("evolution_fft",{}).parameters;
    QCOMPARE(doc.node(evo)->parameters,defaults);
    QVERIFY(doc.node(evo)->parameterOverrides.isEmpty());
    // Neither dataset has ever opened an editor. Resolve from each actual input.
    auto checkRun=[&](int wa,int wb) {
        const auto plan=doc.plan(); QVERIFY(plan.validation.ok()); QCOMPARE(plan.requests.size(),2);
        int index=0;
        for(const auto &request:plan.requests) {
            const auto result=WorkflowExecutor().run(request).nodes[evo];
            QCOMPARE(result.state,NodeState::Succeeded);
            const auto window=index==0?wa:wb;
            QCOMPARE(result.report["window_samples"].toInt(),window);
            QCOMPARE(result.report["nfft"].toInt(),window*4);
            const auto *grid=dataAs<GridData>(result.outputs["power_map"]); QVERIFY(grid);
            QVERIFY(std::abs(grid->y.back()-(index==0?2.:1.))<1e-10);
            ++index;
        }
    };
    checkRun(100,240);
    const auto revision=doc.revision();
    ParameterEditor editor; NodeDefinition shown=*doc.node(evo);
    shown.parameters=effectiveParameters(shown,a); editor.showNode(&shown,doc.model().registry());
    QCOMPARE(doc.revision(),revision); QVERIFY(!editor.dirty());
    auto changed=defaults; changed["evo_window_percent"]=30;
    QVERIFY(doc.setScopedParameters(evo,a,changed).ok());
    checkRun(150,240); // A changes; unvisited B retains defaults.
    QCOMPARE(effectiveParameters(*doc.node(evo),b),defaults);
    QVERIFY(doc.setScopedParameters(evo,a,changed,true).ok());
    checkRun(150,360); // Copies the rule, never A's resolved count.
    QVERIFY(doc.node(evo)->parameterOverrides.isEmpty());
    const auto encoded=workflowToJson(doc.model().definition());
    const auto decoded=workflowFromJson(encoded); QVERIFY(decoded.validation.ok());
    QCOMPARE(decoded.definition.nodes[1].parameters,changed);
    QVERIFY(doc.undo().ok()); checkRun(150,240);
    QVERIFY(doc.redo().ok()); checkRun(150,360);
    // Manual settings remain intentional until reset; reset+batch restores rules.
    changed["evo_window_mode"]="samples"; changed["evo_window"]=80;
    QVERIFY(doc.setScopedParameters(evo,a,changed,true).ok()); checkRun(80,80);
    shown=*doc.node(evo); shown.parameters=effectiveParameters(shown,a);
    editor.showNode(&shown,doc.model().registry());
    QTest::mouseClick(editor.findChild<QPushButton*>("resetNodeDefaults"),Qt::LeftButton);
    QVERIFY(doc.setScopedParameters(evo,a,editor.parameters()).ok()); checkRun(100,80);
    QVERIFY(doc.setScopedParameters(evo,a,editor.parameters(),true).ok()); checkRun(100,240);
    // Input replacement and future datasets inherit rules, not cached numbers.
    auto sources=doc.sources().value(root); sources[1].data=input(801,.5);
    QVERIFY(doc.setSources(root,sources).isEmpty()); checkRun(100,160);
    sources.push_back({"C",input(1001,1.),{}});
    QVERIFY(doc.setSources(root,sources).isEmpty());
    const auto futurePlan=doc.plan(); QVERIFY(futurePlan.validation.ok());
    QCOMPARE(futurePlan.requests.size(),3);
    const auto future=WorkflowExecutor().run(futurePlan.requests.back()).nodes[evo];
    QCOMPARE(future.state,NodeState::Succeeded);
    QCOMPARE(future.report["window_samples"].toInt(),200);
    QCOMPARE(future.report["nfft"].toInt(),800);
    QCOMPARE(future.report["nyquist_frequency"].toDouble(),.5);
}

void UiTests::automaticEvolutionBatchButtons()
{
    QTemporaryDir dir; AppSettings settings; settings.databasePath=dir.filePath("automatic.sqlite");
    QStringList paths;
    for(int n:{501,1201}) {
        const auto path=dir.filePath(QString::number(n)+".csv"); paths<<path;
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly));
        QByteArray bytes("x,y\n");
        for(int i=0;i<n;++i) bytes+=QByteArray::number(i)+","+QByteArray::number(std::sin(i*.13),'g',15)+"\n";
        f.write(bytes);
    }
    MainWindow window(settings,false); window.resize(1500,950); window.show();
    auto *sources=window.findChild<SourcePanel*>(); sources->addFiles(paths);
    QTRY_VERIFY(sources->readyToRun());
    auto *doc=window.document(); const auto root=sources->root();
    const auto node=doc->addChild(root,ChildSlot::Down,"evolution_fft").nodeId;
    const auto a=doc->datasets(node)[0].id,b=doc->datasets(node)[1].id;
    auto *files=sources->findChild<QListWidget*>("sourceFiles"); files->setCurrentRow(0);
    window.canvas()->nodeSelected(node);
    auto *editor=window.findChild<ParameterEditor*>();
    auto *controller=window.findChild<WorkflowPreviewPanel*>()->controller();
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
    QCOMPARE(controller->result()->output.report["window_samples"].toInt(),100);
    auto *percent=editor->findChild<QDoubleSpinBox*>("parameter_evo_window_percent"); QVERIFY(percent);
    // Keyboard edit followed immediately by batch action must flush the draft.
    percent->setFocus(); QTest::keyClick(percent,Qt::Key_A,Qt::ControlModifier); QTest::keyClicks(percent,"30");
    QTest::mouseClick(editor->findChild<QPushButton*>("applyNodeAllData"),Qt::LeftButton);
    QTRY_COMPARE(effectiveParameters(*doc->node(node),a)["evo_window_percent"].toDouble(),30.);
    QCOMPARE(effectiveParameters(*doc->node(node),b)["evo_window_percent"].toDouble(),30.);
    files->setCurrentRow(1); // First visit to B must resolve its own 1201 points.
    QTRY_VERIFY_WITH_TIMEOUT(controller->state()==WorkflowPreviewController::State::Ready &&
        controller->result()->output.report["window_samples"].toInt()==360,15000);
    QTest::mouseClick(editor->findChild<QPushButton*>("resetNodeDefaults"),Qt::LeftButton);
    QTest::mouseClick(editor->findChild<QPushButton*>("applyNodeAllData"),Qt::LeftButton);
    QTRY_COMPARE(effectiveParameters(*doc->node(node),a)["evo_window_percent"].toDouble(),20.);
    QCOMPARE(effectiveParameters(*doc->node(node),b)["evo_window_percent"].toDouble(),20.);
    QTRY_VERIFY_WITH_TIMEOUT(controller->state()==WorkflowPreviewController::State::Ready &&
        controller->result()->output.report["window_samples"].toInt()==240,15000);
    files->setCurrentRow(0);
    QTRY_VERIFY_WITH_TIMEOUT(controller->state()==WorkflowPreviewController::State::Ready &&
        controller->result()->output.report["window_samples"].toInt()==100,15000);
    // Switch before the edit debounce fires, then broadcast B's own settings.
    percent=editor->findChild<QDoubleSpinBox*>("parameter_evo_window_percent");
    percent->setFocus();QTest::keyClick(percent,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(percent,"35");
    files->setCurrentRow(1);
    QCOMPARE(effectiveParameters(*doc->node(node),a)["evo_window_percent"].toDouble(),35.);
    QCOMPARE(editor->parameters()["evo_window_percent"].toDouble(),20.);
    QTest::mouseClick(editor->findChild<QPushButton*>("applyNodeAllData"),Qt::LeftButton);
    QCOMPARE(effectiveParameters(*doc->node(node),a)["evo_window_percent"].toDouble(),20.);
    QTest::mouseClick(window.findChild<QPushButton*>("undoWorkflowEdit"),Qt::LeftButton);
    QCOMPARE(effectiveParameters(*doc->node(node),a)["evo_window_percent"].toDouble(),35.);
    QCOMPARE(editor->parameters()["evo_window_percent"].toDouble(),20.);
    QTest::mouseClick(window.findChild<QPushButton*>("redoWorkflowEdit"),Qt::LeftButton);
    QCOMPARE(effectiveParameters(*doc->node(node),a)["evo_window_percent"].toDouble(),20.);
    QCOMPARE(editor->parameters()["evo_window_percent"].toDouble(),20.);
}

void UiTests::previewMessagesRemainReadable()
{
    PlotView view; view.resize(320,220); view.show();
    const QString shortMessage="无法显示预览\n请检查窗口长度与输入数据。";
    view.clear(shortMessage);
    auto *message=view.findChild<QTextBrowser*>("plotMessage"); QVERIFY(message);
    QTRY_VERIFY(message->isVisible());
    QCOMPARE(message->toPlainText(),shortMessage);
    QVERIFY(message->font().pointSizeF()>=14);
    const QString longMessage=QString("<b>这是纯文本错误信息，不执行 HTML。</b>\n").repeated(200);
    view.clear(longMessage);
    QCOMPARE(message->toPlainText(),longMessage);
    QTRY_VERIFY(message->verticalScrollBar()->maximum()>0);
    QVERIFY(message->font().pointSizeF()>=14);
    QCOMPARE(message->horizontalScrollBar()->maximum(),0);
    QCOMPARE(message->verticalScrollBar()->value(),0);
    message->selectAll(); QVERIFY(message->textCursor().hasSelection());
    const auto capture=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if(!capture.isEmpty()) {QDir().mkpath(capture);view.grab().save(capture+"/preview-long-message.png");}
    view.clear(shortMessage); QTRY_COMPARE(message->verticalScrollBar()->maximum(),0);
    if(!capture.isEmpty()) view.grab().save(capture+"/preview-short-message.png");
}

void UiTests::evolutionResolvedPreviewFollowsInput()
{
    struct Restore { ~Restore(){setLanguage("zh");} } restore;
    for (const auto &language : QStringList{"zh", "en"})
    {
        setLanguage(language);
        WorkflowDocument doc(makeScientificRegistry());
        const auto root=doc.addRoot("sine_source",{{"count",1001},{"step",1}}).nodeId;
        const auto cut=doc.addChild(root,ChildSlot::Down,"select_interval",
            {{"interval_lower",0},{"interval_upper",500}}).nodeId;
        const QJsonObject params{{"evo_window_mode","span_percent"},{"evo_window_percent",20},
            {"evo_step_mode","target_windows"},{"evo_target_windows",500},
            {"evo_fft_mode","multiple"},{"evo_padding",4}};
        const auto target=doc.addChild(cut,ChildSlot::Down,"evolution_fft",params).nodeId;
        QVERIFY(!target.isNull());
        WorkflowPreviewPanel panel(&doc); panel.resize(360,850); panel.show();
        auto *controller=panel.controller(); controller->selectNode(target);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
        // Resolve the 501-point upstream output, not the 1001-point source.
        QCOMPARE(controller->result()->output.report["input_samples"].toInt(),501);
        const auto expected=text("ui.evolution_resolved").arg(100).arg(1).arg(99.,0,'f',2).arg(400)
            + text("ui.preview_warning_count").arg(1);
        QLabel *status=nullptr;
        for (auto *label:panel.findChildren<QLabel*>())
            if (label->text()==expected) status=label;
        QVERIFY(status); QVERIFY(status->toolTip().contains("402"));
        auto changed=params; changed["evo_target_windows"]=20;
        QVERIFY(doc.setParameters(target,changed).ok());
        QVERIFY(status->text()!=expected); // Invalidate the old summary immediately.
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
        QCOMPARE(controller->result()->output.report["step_samples"].toInt(),22);
        QCOMPARE(status->text(),text("ui.evolution_resolved").arg(100).arg(22).arg(78.,0,'f',2).arg(400)
            + text("ui.preview_warning_count").arg(1));
        const auto lastSummary=status->text();
        const auto capture=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
        if (!capture.isEmpty())
        {
            QDir().mkpath(capture);
            panel.grab().save(capture+"/evolution-preview-"+language+".png");
            ParameterEditor editor; editor.resize(330,900);
            editor.showNode(doc.node(target),doc.model().registry()); editor.show();
            auto *preset=editor.findChild<QPushButton*>("resetNodeDefaults"); QVERIFY(preset);
            QTest::mouseClick(preset,Qt::LeftButton); QTest::qWait(10);
            editor.grab().save(capture+"/evolution-parameters-"+language+".png");
        }
        changed["evo_window_percent"]=.01;
        QVERIFY(doc.setParameters(target,changed).ok());
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Failed,15000);
        QVERIFY(status->text()!=expected && status->text()!=lastSummary);
        QCOMPARE(status->text(),text("ui.preview_failed"));
        auto *failedView=panel.findChild<PlotView*>("nodePreviewPlot"); QVERIFY(failedView);
        auto *failure=failedView->findChild<QTextBrowser*>("plotMessage"); QVERIFY(failure);
        QTRY_VERIFY(failure->isVisible());
        const auto explanation=presentError(controller->result()->output);
        QVERIFY(failure->toPlainText().contains(explanation.summary));
        QVERIFY(failure->toPlainText().contains(explanation.suggestion));
        QVERIFY(failure->font().pointSizeF()>=14);
        if(!capture.isEmpty()) panel.grab().save(capture+"/evolution-error-"+language+".png");
        controller->selectNode(root);
        QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,15000);
        QVERIFY(!status->text().contains("FFT"));
    }
}

void UiTests::selectedImportRulesAndApplyAllReachStoredOutputs()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("scope-chain.sqlite");
    MainWindow window(settings, false);
    window.resize(1500, 920);
    window.show();
    auto *doc = window.document();
    auto *sources = window.findChild<SourcePanel *>();
    QStringList paths;
    for (const auto *name : {"a.csv", "b.csv", "c.csv", "other.csv"}) {
        const auto path = dir.filePath(name);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("depth,age,proxy\n100,0,10\n200,1,20\n300,2,30\n");
        paths << path;
    }
    sources->addFiles(paths.mid(0,3));
    const auto root = sources->root();
    const auto scale = doc->addChild(root, ChildSlot::Append, "affine").nodeId;
    const auto sibling = doc->addChild(root, ChildSlot::Append, "affine", {{"factor", 5}}).nodeId;
    const auto other = doc->addRoot("file_source").nodeId;
    doc->setFiles(other, {paths.last()});
    const auto otherScale = doc->addChild(other, ChildSlot::Append, "affine", {{"factor", 9}}).nodeId;
    const auto inputs = doc->datasets(root);
    auto *canvas = window.canvas();
    canvas->fitWorkflow();
    const auto hit = canvas->mapFromScene(canvas->nodeRect(scale).center());
    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, hit);
    QCOMPARE(canvas->selectedNode(), scale);
    auto *list = sources->findChild<QListWidget *>("sourceFiles");
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ShiftModifier, list->visualItemRect(list->item(1)).center());
    QCOMPARE(list->selectedItems().size(), 2);
    QTest::mouseClick(sources->findChild<QToolButton *>("sourceOptionsToggle"), Qt::LeftButton);
    auto *reader = sources->findChild<FileOptionsEditor *>();
    QTRY_VERIFY(reader->ready());
    auto *x = reader->findChild<QComboBox *>("fileXColumn");
    auto *y = reader->findChild<QComboBox *>("fileYColumn");
    y->setFocus();
    QTest::keyClick(y, Qt::Key_End);
    QTRY_COMPARE(doc->fileOptions(root, paths[0])["y_column"].toInt(), 2);
    QTRY_VERIFY(sources->readyToRun());
    x->setFocus();
    QTest::keyClick(x, Qt::Key_Down);
    QTRY_COMPARE(doc->fileOptions(root, paths[1])["x_column"].toInt(), 1);
    QTRY_VERIFY(sources->readyToRun());
    for (const auto &file : paths.mid(0,2)) {
        QCOMPARE(doc->fileOptions(root, file)["x_column"].toInt(), 1);
        QCOMPARE(doc->fileOptions(root, file)["y_column"].toInt(), 2);
    }
    QCOMPARE(doc->fileOptions(root, paths[2])["x_column"].toInt(), 0);
    QCOMPARE(doc->fileOptions(root, paths[2])["y_column"].toInt(), 1);
    auto *editor = window.findChild<ParameterEditor *>();
    auto *factor = editor->findChild<QDoubleSpinBox *>("parameter_factor");
    QVERIFY(factor && factor->isVisible());
    factor->setFocus();
    QTest::keySequence(factor, QKeySequence::SelectAll);
    QTest::keyClicks(factor, "3");
    QTest::keyClick(factor, Qt::Key_Tab);
    const auto effective = [&](int idx) {
        return effectiveParameters(*doc->node(scale), inputs[idx].id)["factor"].toDouble();
    };
    QTRY_COMPARE(effective(1), 3.);
    QCOMPARE(effective(0), 1.);
    QCOMPARE(effective(2), 1.);
    auto *all = editor->findChild<QPushButton *>("applyNodeAllData");
    QTest::mouseClick(all, Qt::LeftButton);
    QTRY_COMPARE(effective(0), 3.);
    QCOMPARE(effective(2), 3.);
    QTest::mouseClick(window.findChild<QPushButton *>("undoWorkflowEdit"), Qt::LeftButton);
    QCOMPARE(effective(0), 1.);
    QCOMPARE(effective(1), 3.);
    QCOMPARE(effective(2), 1.);
    QTest::mouseClick(window.findChild<QPushButton *>("redoWorkflowEdit"), Qt::LeftButton);
    QCOMPARE(effective(2), 3.);
    window.startRun();
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy(), 15000);
    ResultRepository repo;
    QVERIFY(repo.open(settings.databasePath, false).isEmpty());
    const auto rows = repo.list();
    QCOMPARE(rows.records.size(), 4);
    for (const auto &record : rows.records) {
        const auto loaded = repo.load(record.id);
        QVERIFY(loaded.ok());
        const int file = paths.indexOf(record.sourceLabel);
        QVERIFY(file >= 0);
        QCOMPARE(loaded.run.state, RunState::Succeeded);
        const auto data = dataAs<SeriesData>(loaded.run.nodes.value(file == 3 ? otherScale : scale).outputs.value("series"));
        QVERIFY(data && data->x.size() == 3);
        QCOMPARE(data->x[1], file < 2 ? 1. : 200.);
        QCOMPARE(data->y[1], file < 2 ? 60. : file == 2 ? 3. : 9.);
        if (file < 3) {
            const auto side = dataAs<SeriesData>(loaded.run.nodes.value(sibling).outputs.value("series"));
            QVERIFY(side);
            QCOMPARE(side->y[1], file < 2 ? 100. : 5.);
        }
    }
}

void UiTests::scopedEditingAndPointReview()
{
    QTemporaryDir dir; AppSettings settings; settings.databasePath=dir.filePath("scope.sqlite");
    const auto a=dir.filePath("a.csv"),b=dir.filePath("b.csv");
    for(const auto &path:{a,b}) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x,y\n0,2\n1,2\n2,2\n3,90\n4,2\n5,2\n6,2\n"); }
    MainWindow window(settings,false); window.resize(1500,950); window.show();
    auto *sources=window.findChild<SourcePanel*>(); sources->addFiles({a,b}); QTRY_VERIFY(sources->readyToRun());
    auto *doc=window.document(); const auto root=sources->root();
    const auto node=doc->addChild(root,ChildSlot::Down,"affine").nodeId;
    const auto aid=doc->datasets(node)[0].id,bid=doc->datasets(node)[1].id;
    auto *files=sources->findChild<QListWidget*>("sourceFiles"); files->setCurrentRow(0);
    window.canvas()->nodeSelected(node);
    auto *editor=window.findChild<ParameterEditor*>();
    auto *scope=editor->findChild<QLabel*>("parameterScopeLabel"); QVERIFY(scope);
    QVERIFY(scope->text().endsWith("a.csv")); QVERIFY(!scope->text().contains(dir.path()));
    QVERIFY(editor->findChild<QFrame*>("parameterScopeSeparator"));
    auto *factor=editor->findChild<QDoubleSpinBox*>("parameter_factor"); QVERIFY(factor); factor->setValue(3);
    QTRY_COMPARE(effectiveParameters(*doc->node(node),aid)["factor"].toDouble(),3.);
    files->setCurrentRow(1);
    QTRY_COMPARE(editor->findChild<QDoubleSpinBox*>("parameter_factor")->value(),1.);
    auto *all=editor->findChild<QPushButton*>("applyNodeAllData"); QVERIFY(all && all->isEnabled());
    QTest::mouseClick(all,Qt::LeftButton);
    QTRY_COMPARE(effectiveParameters(*doc->node(node),aid)["factor"].toDouble(),1.);
    QVERIFY(doc->undo().ok());
    QCOMPARE(effectiveParameters(*doc->node(node),aid)["factor"].toDouble(),3.);
    QCOMPARE(effectiveParameters(*doc->node(node),bid)["factor"].toDouble(),1.);
    files->setCurrentRow(0); QTRY_COMPARE(editor->findChild<QDoubleSpinBox*>("parameter_factor")->value(),3.);
    const auto both=doc->sources().value(root);
    QVERIFY(doc->setSources(root,{both.front()}).isEmpty());
    QTRY_VERIFY(!editor->findChild<QPushButton*>("applyNodeAllData")->isEnabled());
    QVERIFY(doc->setSources(root,both).isEmpty());
    QTRY_VERIFY(editor->findChild<QPushButton*>("applyNodeAllData")->isEnabled());
    const auto outlier=doc->addChild(node,ChildSlot::Down,"outlier_handling",{{"outlier_window",3}}).nodeId;
    window.canvas()->nodeSelected(outlier);
    auto *panel=window.findChild<WorkflowPreviewPanel*>(); auto *controller=panel->controller();
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,10000);
    auto *view=panel->findChild<PlotView*>("nodePreviewPlot"); QVERIFY(view);
    QCOMPARE(controller->result()->output.report["detected_count"].toInt(),1);
    const auto fingerprint=controller->result()->output.report["outlier_input_fingerprint"].toString();
    view->pointDecisionRequested(fingerprint,3,0);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,10000);
    QCOMPARE(controller->result()->output.report["selected_count"].toInt(),0);
    const auto pointDecision=effectiveParameters(*doc->node(outlier),aid)["point_overrides"].toString(); QVERIFY(!pointDecision.isEmpty());
    QVERIFY(effectiveParameters(*doc->node(outlier),bid)["point_overrides"].toString().isEmpty());
    all=editor->findChild<QPushButton*>("applyNodeAllData"); QTest::mouseClick(all,Qt::LeftButton);
    QCOMPARE(effectiveParameters(*doc->node(outlier),aid)["point_overrides"].toString(),pointDecision);
    QVERIFY(effectiveParameters(*doc->node(outlier),bid)["point_overrides"].toString().isEmpty());
    files->setCurrentRow(1); QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,10000);
    QCOMPARE(controller->result()->output.report["selected_count"].toInt(),1);
    const auto generated=doc->addRoot("sine_source",{{"count",64}}).nodeId;
    const auto scaled=doc->addChild(generated,ChildSlot::Down,"affine",{{"factor",1}}).nodeId;
    window.canvas()->nodeSelected(scaled);
    editor->findChild<QDoubleSpinBox*>("parameter_factor")->setValue(7);
    QTRY_COMPARE(effectiveParameters(*doc->node(scaled),generated)["factor"].toDouble(),7.);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(),WorkflowPreviewController::State::Ready,10000);
    const auto *original=dataAs<SeriesData>(controller->result()->original.outputs["series"]);
    const auto *output=dataAs<SeriesData>(controller->result()->output.outputs["series"]);
    QVERIFY(original && output); QCOMPARE(output->y[1],original->y[1]*7);

}

void UiTests::cancelAndCloseDuringLowess_data()
{
    QTest::addColumn<bool>("cancelFirst");
    QTest::newRow("cancel-then-close") << true;
    QTest::newRow("close-running-window") << false;
}
void UiTests::cancelAndCloseDuringLowess()
{
    QFETCH(bool, cancelFirst);
    QTemporaryDir dir;
    const auto small = dir.filePath("first.csv"), longInput = dir.filePath("sparse.csv");
    {
        QFile file(small); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Age,Value\n0,4\n1,2\n2,1\n3,5\n4,1\n5,3\n");
    }
    {
        QFile file(longInput); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Age,Value\n");
        for (int i=0; i<427; ++i)
            file.write(QByteArray::number(i)+','+QByteArray::number(std::sin(.31*i))+'\n');
        file.write("555575,3\n");
    }
    AppSettings settings; settings.language="en"; settings.concurrency=1;
    settings.databasePath=dir.filePath("cancel.sqlite");
    MainWindow window(settings,false); window.show();
    const auto root=window.document()->addRoot("file_source").nodeId;
    auto parent=root;
    for (const auto* name : {"clean_series","preprocess","detrend_lowess","mtm"}) {
        // Exercise cancellation inside the fit after explicitly allowing this synthetic gap.
        QJsonObject parameters;
        if (QString::fromLatin1(name) == "preprocess") parameters.insert("allow_extreme_gap", true);
        const auto edit=window.document()->addChild(parent,ChildSlot::Append,name,parameters);
        QVERIFY(edit.ok()); parent=edit.nodeId;
    }
    QStringList paths{small,longInput};
    for (int j=0; j<40; ++j) {
        const auto spare=dir.filePath(QString("pending-%1.csv").arg(j));
        QVERIFY(QFile::copy(small,spare)); paths << spare;
    }
    window.document()->setFiles(root,paths);
    QTRY_VERIFY(window.findChild<SourcePanel*>()->readyToRun());
    bool closing=false;
    std::shared_ptr<const BatchResult> finished;
    connect(window.runner(), &RunController::batchReady, &window,
            [&](std::shared_ptr<const BatchResult> batch) { finished=std::move(batch); });
    connect(window.runner(), &RunController::eventReady, &window, [&](const RunEvent& e) {
        if (closing || e.message!="progress.lowess_fit" || !e.sourceLabel.endsWith("sparse.csv")) return;
        closing=true;
        if (cancelFirst) window.findChild<QPushButton*>("cancelRun")->click();
        else QTimer::singleShot(0, [] {
            if (auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Yes)->click();
        });
        window.close();
    });
    window.startRun();
    QTRY_VERIFY_WITH_TIMEOUT(closing && !window.isVisible() && !window.runner()->busy(),10000);
    QVERIFY(finished);
    QCOMPARE(finished->notStarted, qsizetype(40));
    QCOMPARE(finished->items.size(), 2);
    QCOMPARE(finished->items[0].computation,RunState::Succeeded);
    QCOMPARE(finished->items[1].computation,RunState::Cancelled);
    ResultRepository repo; QVERIFY(repo.open(settings.databasePath).isEmpty());
    QCOMPARE(repo.list().records.size(), 2);
    for (const auto& item : finished->items) {
        QVERIFY(item.persistence.saved());
        const auto stored=repo.load(item.persistence.recordId);
        QVERIFY(stored.ok());
        QCOMPARE(stored.run.state,item.computation);
    }
}

void UiTests::liveDirectoryDuringLargeFileBatch_data()
{
    QTest::addColumn<QVector<int>>("lengths");
    QTest::addColumn<int>("workers");
    QTest::addColumn<bool>("background");
    QTest::newRow("four-workers") << QVector<int>{256} << 4 << false;
    QTest::newRow("eight-workers-full-mtm") << QVector<int>{512, 768, 1024, 1536, 2048} << 8 << true;
}
void UiTests::liveDirectoryDuringLargeFileBatch()
{
    QFETCH(QVector<int>, lengths);
    QFETCH(int, workers);
    QFETCH(bool, background);
    constexpr int fileCount = 2200, checkedCount = 391;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVector<QByteArray> inputs;
    for (const int count : lengths) {
        QByteArray csv("x,y\n");
        for (int i = 0; i < count; ++i) {
            const double y = std::sin(i * 0.3926990817) + 0.4 * std::sin(i * 0.19);
            csv += QByteArray::number(i * 100) + ',' + QByteArray::number(y, 'g', 12) + '\n';
        }
        inputs.append(std::move(csv));
    }
    QStringList paths;
    for (int i = 0; i < fileCount; ++i) {
        paths << dir.filePath(QString("input_%1.csv").arg(i));
        QFile file(paths.back());
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto &bytes = inputs[i % inputs.size()];
        QCOMPARE(file.write(bytes), bytes.size());
    }
    AppSettings settings;
    settings.databasePath = dir.filePath("live.sqlite");
    settings.concurrency = workers;
    const QJsonObject mtm{{"ar1", background}, {"swa", background}};
    WorkflowModel seed(makeScientificRegistry());
    const auto seedRoot = seed.addRoot("file_source", {0, 0}).nodeId;
    QVERIFY(seed.addChild(seedRoot, ChildSlot::Down, "mtm", mtm).ok());
    auto record = WorkflowExecutor().run({seed.freeze().snapshot, seedRoot,
                                          {"marked", {}, {{"path", paths.front()}}}});
    QCOMPARE(record.state, RunState::Succeeded);
    ResultRepository reader;
    QVERIFY(reader.open(settings.databasePath).isEmpty());
    QSet<RunId> historical;
    for (int i = 0; i < checkedCount; ++i) {
        record.runId = QUuid::createUuid();
        record.sourceLabel = QString("marked_%1.csv").arg(i);
        QVERIFY(reader.save(record).saved());
        historical.insert(record.runId);
    }
    MainWindow window(settings, false);
    window.resize(1100, 760);
    window.show();
    const auto root = window.document()->addRoot("file_source").nodeId;
    QVERIFY(window.document()->addChild(root, ChildSlot::Append, "mtm", mtm).ok());
    window.document()->setFiles(root, paths);
    window.showResults();
    auto *panel = window.findChild<RecordDirectoryPanel *>();
    auto *select = panel->findChild<QPushButton *>("checkMatchingRecords");
    auto *remove = panel->findChild<QPushButton *>("deleteCheckedRecords");
    auto *progress = window.findChild<QProgressBar *>("runProgress");
    panel->findChild<QLineEdit *>("recordSearch")->setText("marked_");
    panel->findChild<QComboBox *>("recordPeakAxis")->setCurrentIndex(0);
    panel->findChild<QLineEdit *>("minimumFrequency")->setText("0.0001");
    panel->findChild<QLineEdit *>("maximumFrequency")->setText("0.001");
    QTRY_VERIFY(select->isEnabled());
    select->click();
    QTRY_COMPARE(panel->checkedRecords().size(), checkedCount);
    QTRY_VERIFY(remove->isEnabled());
    QTRY_VERIFY(window.findChild<SourcePanel *>()->readyToRun());
    QSet<RunId> saved;
    int unsuccessful = 0, progressTicks = 0;
    QObject::connect(window.runner(), &RunController::itemReady, &window, [&](const BatchItem &item) {
        if (item.persistence.saved()) saved.insert(item.runId);
        if (!item.persistence.saved() || item.computation != RunState::Succeeded) ++unsuccessful;
    });
    QObject::connect(progress, &QProgressBar::valueChanged, &window, [&](int value) {
        if (value > 0 && value < fileCount * 2) ++progressTicks;
    });
    QElapsedTimer elapsed;
    elapsed.start();
    window.startRun();
    QVERIFY(window.runner()->busy());
    auto *sources = window.findChild<SourcePanel *>();
    auto *inputsList = sources->findChild<QListWidget *>("sourceFiles");
    QVERIFY(inputsList->isEnabled()); QVERIFY(!inputsList->alternatingRowColors());
    QVERIFY(!sources->findChild<QPushButton *>("importFiles")->isEnabled());
    QVERIFY(!sources->findChild<QPushButton *>("removeSourceInput")->isEnabled());
    QVERIFY(!sources->findChild<FileOptionsEditor *>()->isEnabled());
    QSignalSpy probes(sources,&SourcePanel::previewInputChanged);
    inputsList->setCurrentRow(1);
    const auto *current = inputsList->currentItem();
    QVERIFY(current);
    QCOMPARE(current->data(Qt::UserRole).toString(), paths.at(1));
    const auto tooltip = unwrappedFileToolTip(current);
    QVERIFY2(tooltip.contains(paths.at(1)), qPrintable(tooltip));
    QCOMPARE(probes.size(),0);
    QTRY_VERIFY_WITH_TIMEOUT(saved.size() > 10 && progress->value() > 0, 30000);
    window.results()->refresh();
    QTRY_VERIFY(!window.results()->databaseBusy());
    QVERIFY(window.runner()->busy());
    QVERIFY(!remove->isEnabled());
    QCOMPARE(panel->checkedRecords().size(), checkedCount);
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy(), 120000);
    QTRY_VERIFY(!window.results()->databaseBusy());
    qInfo() << "large batch: files" << saved.size() << "progress updates" << progressTicks
            << "elapsed ms" << elapsed.elapsed();
    QCOMPARE(unsuccessful, 0);
    QCOMPARE(saved.size(), fileCount);
    QVERIFY(progressTicks >= (background ? 2 : 1));
    QTRY_VERIFY(remove->isEnabled());
    QCOMPARE(progress->value(), fileCount * 2);
    const auto rows = reader.identities({});
    QVERIFY2(rows.error.isEmpty(), qPrintable(rows.error));
    QCOMPARE(rows.existing.size(), fileCount + checkedCount);
    const auto expected = historical | saved;
    for (const auto &runId : rows.existing) QVERIFY(expected.contains(runId));
    const auto loaded = reader.load(rows.existing.lastKey());
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.run.workflowSnapshot.value("schema_version").toInt(), 3);
    RecordFilter frequency;
    frequency.minFrequency = 0.0001;
    frequency.maxFrequency = 0.001;
    QCOMPARE(reader.identities(frequency).matching, fileCount + checkedCount);
    const auto connection = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(settings.databasePath);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("PRAGMA integrity_check") && query.next());
        QCOMPARE(query.value(0).toString(), QString("ok"));
        QVERIFY(query.exec("PRAGMA foreign_key_check"));
        QVERIFY(!query.next());
    }
    QSqlDatabase::removeDatabase(connection);
}

void UiTests::appendEightChildrenFromCanvasAndCatalogue()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("branches.sqlite");
    MainWindow window(settings, false);
    window.resize(1200, 800);
    window.show();
    auto *doc = window.document();
    auto *canvas = window.canvas();
    auto *catalog = window.findChild<TaskCatalog *>();
    const auto root = doc->addRoot("sine_source").nodeId;
    canvas->fitWorkflow();
    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->addViewportPosition(root));
    QTRY_VERIFY(QApplication::activePopupWidget());
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    QVERIFY(menu);
    auto *action = findAction(menu, "demean");
    QVERIFY(action);
    action->trigger();
    menu->close();
    QCOMPARE(doc->model().childCount(root), 1);
    for (int i = 1; i < WorkflowChildLimit; ++i) {
        canvas->nodeSelected(root);
        catalog->methodActivated("demean");
        QCOMPARE(doc->model().childCount(root), i + 1);
    }
    const auto before = workflowToJson(doc->model().definition());
    QCOMPARE(canvas->addViewportPosition(root), QPoint(-1, -1));
    canvas->nodeSelected(root);
    catalog->methodActivated("demean");
    QCOMPARE(workflowToJson(doc->model().definition()), before);
    const auto nodes = doc->model().definition().nodes;
    for (qsizetype i = 1; i < nodes.size(); ++i) {
        QCOMPARE(nodes[i].input->parent, root);
        QCOMPARE(nodes[i].input->slot, ChildSlot::Append);
        const auto rect = canvas->nodeRect(nodes[i].id);
        QVERIFY(rect.top() > canvas->nodeRect(root).bottom());
        if (i > 1) QVERIFY(rect.left() > canvas->nodeRect(nodes[i-1].id).right());
    }
    QVERIFY(doc->removeSubtree(nodes[3].id).ok());
    QVERIFY(canvas->addViewportPosition(root) != QPoint(-1, -1));
    QVERIFY(doc->undo().ok());
    QCOMPARE(workflowToJson(doc->model().definition()), before);
    // Insert still works when all eight branches are occupied, and undo restores
    // every sibling and its output binding.
    const auto edit = doc->prepareRewrite({NodeRewriteKind::InsertBefore, nodes[4].id,
        "demean", "series", {{nodes[4].id, "series"}}, {}});
    QVERIFY2(edit.ok(), qPrintable(edit.error));
    QVERIFY(doc->commitEdit(edit.edit).ok());
    QCOMPARE(doc->model().childCount(root), WorkflowChildLimit);
    QCOMPARE(doc->node(nodes[4].id)->input->parent, edit.nodeId);
    QVERIFY(doc->undo().ok());
    QCOMPARE(workflowToJson(doc->model().definition()), before);
}
