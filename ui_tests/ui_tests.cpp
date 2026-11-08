// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QTextBlock>
#include <QTextFragment>
#include "ui/help/help_dialog.h"
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
#include <QTableView>
#include <QtTest/qtestspontaneevent.h>
#include <QApplication>
#include <QListWidget>
#include <cmath>
#include "ui/settings/theme_manager.h"
#include <QSplitter>
#include "ui/panels/workflow_preview_panel.h"
#include "ui/panels/collapsible_panel.h"
#include "ui/services/workflow_preview_controller.h"
#include "ui/results/preview_data.h"
#include "io/database/data_codec.h"
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
    void licenseNoticesOffline();
    void parameterHelpTracksSelectedNode();
    void fileReadinessSurvivesDisabledPanel();
    void compactInputStatesAndFrequencyEditor();
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
    void frequencyFilterShowsMatchingNode();
    void closeWaitsForDatabaseEdits();
    void fileOptionsRemainPerInput();
    void directoryServiceQueriesAndMutations();
    void nodeActionsAndFrequencyFilter();
    void initTestCase();
    void draftBindingsSurviveUndo();
    void sourceBindingsAreSeparate();
    void hierarchicalTaskMenu();
    void astronomySourcesUseSharedUi();
    void parameterValuesAreIndependent();
    void parameterRoundTrip_data();
    void parameterRoundTrip();
    void scientificParameterTyping();
    void scientificParametersReachPreviewAndRun();
    void verticalLayoutPreservesWorkflow();
    void persistentAddControlsUseTheirOwnParent();
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
    QCOMPARE(capturedHelpUrls.last().toString(), QString("https://github.com/ChenCLian/Aster/blob/main/docs/missing-manual.md#missing-section"));
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
    QCOMPARE(capturedHelpUrls.last().toString(), QString("https://github.com/ChenCLian/Aster/tree/main/licenses"));
    QCOMPARE(dialog.currentTopic().path, QString("AUTHORS.md"));
    QVERIFY(localHelpAvailable({"docs/methods/", {}}));
    QVERIFY(dialog.navigate({"docs/README.md", {}}));
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
                                     Q_ARG(QUrl, QUrl("methods/"))));
    QCOMPARE(dialog.currentTopic().path, QString("docs/methods/README.md"));
    QVERIFY(dialog.navigate(userGuideTopic()));
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
        Q_ARG(QUrl, QUrl("https://github.com/ChenCLian/Aster/tree/main/docs/methods"))));
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
    QVERIFY(!button->toolTip().contains("parameterhelp."));
    button->click();
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
    row->findChild<QToolButton *>("ParameterHelp")->click();
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
        QCOMPARE(capturedHelpUrls.last().toString(), QString("https://github.com/ChenCLian/Aster/blob/main/AUTHORS.md"));
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
    auto pos = canvas.slotViewportPosition(ChildSlot::Down);
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
    auto def = workflowFromJson(run.run.workflowSnapshot).definition;
    for (const auto &n : def.nodes)
        if (n.method == "mtm")
            QCOMPARE(n.parameters["nw"].toInt(), 4);
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
    outputs->model()->setData(outputs->model()->index(0, 0), Qt::Checked, Qt::CheckStateRole);
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
void UiTests::frequencyFilterShowsMatchingNode()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("frequency.sqlite");
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("sine_source", {0, 0}).nodeId;
    const auto node =
        model.addChild(root, ChildSlot::Down, "mtm", {{"ar1", false}, {"swa", false}}).nodeId;
    auto run =
        WorkflowExecutor().run({model.freeze().snapshot, root, {"frequency fixture", {}, {}}});
    QCOMPARE(run.state, RunState::Succeeded);
    run.nodes[node] = spectrumResult();
    qint64 id;
    {
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        id = repo.save(run).recordId;
        QVERIFY(id > 0);
    }
    ResultsPage page(makeScientificRegistry());
    page.setDatabase(path);
    auto *unit = page.findChild<QComboBox *>("frequencyUnit");
    QTRY_VERIFY(unit->findData("1/ka") >= 0);
    unit->setCurrentIndex(unit->findData("1/ka"));
    auto *low = page.findChild<QLineEdit *>("minimumFrequency");
    auto *high = page.findChild<QLineEdit *>("maximumFrequency");
    low->setText("0.09");
    high->setText("0.11");
    QTest::qWait(300);
    QTRY_VERIFY(!page.databaseBusy());
    QCOMPARE(page.recordCount(), 1);
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(id);
    QTRY_COMPARE(loaded.count(), 1);
    auto *steps = page.findChild<QTableWidget *>("resultSteps");
    QVERIFY(steps->item(1, 2)->text().contains(QString::fromUtf8("峰命中")));
    QVERIFY(steps->item(1, 2)->toolTip().contains("0.1 1/ka"));
    low->setText("0.15");
    high->setText("0.16");
    QTRY_COMPARE(page.recordCount(), 0);
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
    QTRY_COMPARE(window.document()->node(child)->parameters["factor"].toDouble(),3.);
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
    QVERIFY(column->width()>=300&&column->width()<=720);
    const auto capture=qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if(!capture.isEmpty()) {
        QDir().mkpath(capture);QTest::qWait(60);QVERIFY(window.grab().save(capture+"/workflow_preview.png"));
        ThemeManager::instance().apply(*qApp,"dark");QTest::qWait(60);QVERIFY(window.grab().save(capture+"/workflow_preview_dark.png"));
        ThemeManager::instance().apply(*qApp,"light");window.resize(1366,768);QTest::qWait(60);
        QVERIFY(window.grab().save(capture+"/workflow_preview_1366.png"));
    }
    column->setCollapsed(true);QCOMPARE(preview->controller()->state(),WorkflowPreviewController::State::Paused);
    column->setCollapsed(false);QTRY_COMPARE(preview->controller()->state(),WorkflowPreviewController::State::Ready);
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
    const auto pos = canvas.slotViewportPosition(ChildSlot::Down);
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
            QCOMPARE(plot->plottedCount(), 1); // Independent ports keep their own axes.
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
        NodeDefinition n{QUuid::createUuid(), "demean", 1, {}, std::nullopt};
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
    QCOMPARE(qvariant_cast<ChildSlot>(activations[0][1]), ChildSlot::Down);
    QMimeData mime; mime.setData(TaskMimeType, "demean");
    QDragEnterEvent enter(compact, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &enter);
    QDropEvent drop(compact, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &drop);
    QCOMPARE(dropped.count(), 1); QCOMPARE(dropped[0][1].toUuid(), child);
    for (auto slot : {ChildSlot::Left, ChildSlot::Right}) {
        QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, canvas.slotViewportPosition(slot));
        QCOMPARE(activations.last()[0].toUuid(), root);
        QCOMPARE(qvariant_cast<ChildSlot>(activations.last()[1]), slot);
    }
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
        QVERIFY(!normalize->isChecked());
        QVERIFY(editor.findChild<QLabel *>("parameterLabel_frequency_max")->toolTip().contains("1/m"));
        QVERIFY(editor.findChild<QLabel *>("parameterLabel_evo_step")->toolTip().contains("37"));
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
        QVERIFY(!view.style().colorInterpolate);
        QVERIFY(!view.style().colorLog);
        view.showOutput(relative, "power_map");
        QVERIFY(!view.style().colorInterpolate);
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
void UiTests::compactInputStatesAndFrequencyEditor()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("file_source").nodeId;
    const QString path = QDir::temp().absoluteFilePath("Cau-GR-sue-rsp0.02-28.732-LOWESS.csv");
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
        QVERIFY(item->toolTip().contains(path));
        QVERIFY(item->toolTip().contains(item->data(Qt::UserRole + 1).toString()));
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
        QVERIFY(list->visualItemRect(item).height() <= panel.fontMetrics().height() + 18);
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
    auto *select = panel.findChild<QPushButton *>("checkMatchingRecords");
    auto *unselect = panel.findChild<QPushButton *>("uncheckMatchingRecords");
    auto *clear = panel.findChild<QPushButton *>("clearRecordChecks");
    auto *search = panel.findChild<QLineEdit *>("recordSearch");
    QVERIFY(select && unselect && clear && search);
    QVERIFY(select->isVisible() && unselect->isVisible() && clear->isVisible());
    QVERIFY(!select->menu());
    QTRY_VERIFY(select->isEnabled()); select->click();
    QTRY_COMPARE(panel.checkedRecords().size(), 2);
    search->setText("keep.csv");
    QTRY_COMPARE(panel.recordCount(), 1);
    QCOMPARE(panel.checkedRecords().size(), 2);
    QTRY_VERIFY(unselect->isEnabled()); unselect->click();
    QTRY_COMPARE(panel.checkedRecords().size(), 1);
    QVERIFY(panel.checkedRecords().contains(remove));
    QVERIFY(!panel.checkedRecords().contains(keep));
    QTRY_VERIFY(select->isEnabled());
    select->click(); QTRY_COMPARE(panel.checkedRecords().size(), 2);
    // Selection changes are followed by an asynchronous directory refresh.
    // The new count alone does not mean that another action can run yet.
    QTRY_VERIFY(clear->isEnabled());
    clear->click(); QCOMPARE(panel.checkedRecords().size(), 0);
    search->clear(); QTRY_COMPARE(panel.recordCount(), 2);
    auto *list = panel.findChild<QListWidget *>("resultRecords"); list->setCurrentRow(0);
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
    QVERIFY(window.document()->node(target)->parameters["factor"].toDouble() == 1e-15);
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
