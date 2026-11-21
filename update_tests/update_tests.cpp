// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "aster_version.h"
#include "update/update_checker.h"
#include "ui/settings/settings_dialog.h"
#include "ui/settings/update_dialog.h"
#include "ui/common/presentation.h"
#include "ui/settings/theme_manager.h"
#include "ui/panels/parameter_editor.h"
#include "ui/help/help_dialog.h"
#include <QTextBrowser>
#include <QFileDialog>
#include <QCryptographicHash>
#include <QTabBar>
#include <QDir>
#include <QScopeGuard>
#include <QTextDocument>
#include "ui/workflow/workflow_document.h"
#include "ui/app/main_window.h"
#include "ui/settings/language_catalog.h"
#include "processing/scientific_registry.h"
#include <QtTest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionComboBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstring>
using namespace aster::updates;
namespace {
// Exercise each update channel even when the source tree itself is an alpha.
auto useApplicationVersion(const QString &version)
{
    const auto previous = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationVersion(version);
    return qScopeGuard([previous] { QCoreApplication::setApplicationVersion(previous); });
}
struct LanguageEdit { bool submitted=false, prompted=false, workflowNotice=false, timedOut=false; };
LanguageEdit editLanguage(aster::ui::MainWindow &window, const QString &language,
                         QMessageBox::StandardButton answer, bool cancelSettings=false)
{
 LanguageEdit result;QElapsedTimer elapsed;elapsed.start();QTimer poll;
 QObject::connect(&poll,&QTimer::timeout,&window,[&] {
  if(elapsed.elapsed()>5000) {
   result.timedOut=true;
   if(auto *dialog=qobject_cast<QDialog*>(qApp->activeModalWidget()))dialog->reject();
   return;
  }
  if(auto *prompt=window.findChild<QMessageBox*>("languageRestartPrompt");prompt && prompt->isVisible()) {
   result.prompted=true;result.workflowNotice=prompt->text().contains(aster::ui::text("ui.restart_workflow_notice"));
   prompt->button(answer)->click();return;
  }
  if(auto *dialog=window.findChild<QDialog*>("settingsDialog");dialog && dialog->isVisible() && !result.submitted) {
   auto *combo=dialog->findChild<QComboBox*>("settingsLanguage");
   combo->setCurrentIndex(combo->findData(language));result.submitted=true;
   dialog->findChild<QDialogButtonBox*>()->button(cancelSettings?QDialogButtonBox::Cancel:QDialogButtonBox::Ok)->click();
  }
 });
 poll.start(5);window.findChild<QPushButton*>("openSettings")->click();poll.stop();return result;
}
UpdateConfig config(){return {"ChenCLian","AstroBatch","2026-03-10"};}
QJsonObject release(QString tag,bool pre=false,bool draft=false){return {{"tag_name",tag},{"prerelease",pre},{"draft",draft},{"name",tag},{"body","Plain release notes"}};}
QByteArray json(QJsonArray a){return QJsonDocument(a).toJson(QJsonDocument::Compact);}
struct ReplyData {int status=200;QByteArray body="[]";int delay=1;QByteArray rate{};};
class FakeReply:public QNetworkReply {
public:
 FakeReply(const QNetworkRequest &r,ReplyData data,QObject *parent):QNetworkReply(parent),m_body(std::move(data.body)) {
  setRequest(r);setUrl(r.url());setOperation(QNetworkAccessManager::GetOperation);setAttribute(QNetworkRequest::HttpStatusCodeAttribute,data.status);
  if(!data.rate.isEmpty())setRawHeader("X-RateLimit-Remaining",data.rate);
  open(QIODevice::ReadOnly);
  QTimer::singleShot(data.delay,this,[this]{if(isFinished())return;emit readyRead();if(!isFinished()){setFinished(true);emit finished();}});
 }
 void abort() override {if(isFinished())return;setError(OperationCanceledError,"Cancelled");setFinished(true);emit finished();}
 qint64 bytesAvailable()const override{return m_body.size()-m_at+QNetworkReply::bytesAvailable();}
 bool isSequential()const override{return true;}
protected:
 qint64 readData(char *p,qint64 max)override {const auto n=qMin<qint64>(max,m_body.size()-m_at);if(n<=0)return -1;std::memcpy(p,m_body.constData()+m_at,size_t(n));m_at+=n;return n;}
private:QByteArray m_body;qint64 m_at=0;
};
class FakeManager:public QNetworkAccessManager {
public:QList<ReplyData> responses;QList<QNetworkRequest> requests;
protected:QNetworkReply *createRequest(Operation,const QNetworkRequest &r,QIODevice *)override {
 requests.append(r);return new FakeReply(r,responses.isEmpty()?ReplyData{500,"[]"}:responses.takeFirst(),this);
 }
};
class LoopbackManager:public QNetworkAccessManager {
public:quint16 port=0;QUrl requested;
protected:QNetworkReply *createRequest(Operation op,const QNetworkRequest &r,QIODevice *d)override {
 requested=r.url();auto local=r;QUrl u=r.url();u.setScheme("http");u.setHost("127.0.0.1");u.setPort(port);local.setUrl(u);return QNetworkAccessManager::createRequest(op,local,d);
 }
};
}
class UpdateTests:public QObject {
 Q_OBJECT
private slots:


 void cancelMetadataCheckKeepsCancelledState(){
  const auto version = useApplicationVersion("1.3.0-beta.1");
  auto cfg=config();cfg.packageMetadata="AstroBatch-update.json";cfg.mirrorFallback=true;
  FakeManager manager;manager.responses={{200,json({release("v99.0.0-beta.1",true)})},{200,"{}",10000}};
  aster::ui::UpdateDialog dialog(cfg,nullptr,&manager);dialog.show();
  auto *downloader=dialog.findChild<PackageDownload*>();QVERIFY(downloader);QTRY_COMPARE(manager.requests.size(),2);QVERIFY(downloader->busy());
  QTest::mouseClick(dialog.findChild<QPushButton*>("updateCancel"),Qt::LeftButton);
  QVERIFY(!downloader->busy());QCOMPARE(dialog.findChild<QLabel*>("updateHeading")->text(),aster::ui::text("update.cancelled_heading"));
  QCOMPARE(dialog.findChild<QPushButton*>("updateOpen")->text(),aster::ui::text("update.retry"));QCOMPARE(manager.requests.size(),2);
  dialog.reject();
 }
 void unavailableManifestShowsBothSites(){
  const auto version=useApplicationVersion("1.3.0-beta.1");
  auto cfg=config();cfg.packageMetadata="AstroBatch-update.json";cfg.mirrorFallback=true;
  FakeManager manager;manager.responses={{200,json({release("v99.0.0-beta.1",true)})},{503,{}},{404,{}},{404,{}}};
  aster::ui::UpdateDialog dialog(cfg,nullptr,&manager);dialog.show();
  QTRY_COMPARE(manager.requests.size(),4);QTRY_VERIFY(!dialog.findChild<PackageDownload*>()->busy());
  auto *notesSite=dialog.findChild<QLabel*>("updateSourceLabel");
  QCOMPARE(notesSite->text(),aster::ui::text("update.announcement_source").arg("GitHub"));
  QVERIFY(!dialog.findChild<QComboBox*>("updateDownloadSource")->isVisible());
  QCOMPARE(dialog.findChild<QPushButton*>("updateOpen")->text(),aster::ui::text("update.open_release"));
  dialog.findChild<QToolButton*>("updateDetails")->click();
  auto *why=dialog.findChild<QLabel*>("updateDiagnostics");
  QVERIFY(why->isVisible());QVERIFY(why->text().contains("GitHub"));QVERIFY(why->text().contains("Gitee"));
 }
 void giteePartialResultNeverClaimsLatest(){
  const auto version = useApplicationVersion("1.3.0-beta.1");
  auto cfg=config();cfg.mirrorFallback=true;FakeManager manager;manager.responses={{503,"[]"},{200,"[]"}};
  aster::ui::UpdateDialog dialog(cfg,nullptr,&manager);dialog.show();
  QTRY_COMPARE(manager.requests.size(),2);QTRY_VERIFY(!dialog.findChild<UpdateChecker*>()->busy());
  QCOMPARE(dialog.findChild<QLabel*>("updateHeading")->text(),aster::ui::text("update.mirror_no_new"));
  QCOMPARE(dialog.findChild<QLabel*>("updateStatus")->text(),aster::ui::text("update.source_partial_hint"));
  const auto screenshots=qEnvironmentVariable("ASTER_UPDATE_SCREENSHOT_DIR");
  if(!screenshots.isEmpty()){QDir().mkpath(screenshots);QVERIFY(dialog.grab().save(screenshots+"/gitee-partial.png"));}
  dialog.reject();
 }
 void giteeFallback_data(){QTest::addColumn<QString>("mode");for(const auto *mode:{"timeout","server","pagination","both-fail","current","empty","alpha"})QTest::newRow(mode)<<QString(mode);}
 void giteeFallback(){
  QFETCH(QString,mode);auto cfg=config();cfg.mirrorFallback=true;cfg.timeoutMs=30;
  FakeManager manager;QJsonArray rows;auto mirrored=release(mode=="current"?"v1.0.0":"v1.1.0");mirrored.remove("draft");rows.append(mirrored);rows.append(release("v9.0.0-alpha.1",true));
  if(mode=="pagination"){QJsonArray page;for(int i=0;i<100;++i)page.append(release("v99.0.0"));manager.responses.append({200,json(page)});}
  manager.responses.append(mode=="timeout"?ReplyData{200,"[]",300}:ReplyData{503,"[]"});
  manager.responses.append(mode=="both-fail"?ReplyData{503,"[]"}:ReplyData{200,mode=="empty"?QByteArray("[]"):json(rows)});
  UpdateChecker checker(cfg,nullptr,&manager);QSignalSpy done(&checker,&UpdateChecker::finished);
  checker.check(mode=="alpha"?"1.0.0-alpha.1":"1.0.0",true);
  if(done.isEmpty())QVERIFY(done.wait());QCOMPARE(done.size(),1);
  const auto result=qvariant_cast<UpdateResult>(done[0][0]);
  if(mode=="alpha"){QVERIFY(manager.requests.isEmpty());return;}
  QVERIFY(result.partial);QCOMPARE(manager.requests.last().url(),cfg.apiUrl(1,true));
  QVERIFY(!manager.requests.last().hasRawHeader("Authorization"));QVERIFY(!manager.requests.last().hasRawHeader("X-GitHub-Api-Version"));
  if(mode=="both-fail")QCOMPARE(result.state,UpdateResult::State::Failed);
  else if(mode=="empty")QCOMPARE(result.state,UpdateResult::State::NoReleases);
  else {QVERIFY(result.release);QCOMPARE(result.release->source,QString("Gitee"));QCOMPARE(result.release->pageUrl.host(),QString("gitee.com"));QCOMPARE(result.release->tag,mode=="current"?QString("v1.0.0"):QString("v1.1.0"));}
 }

 void alphaExcludedFromLegacyChecker() {
  FakeManager manager; UpdateChecker checker(config(),nullptr,&manager);
  QSignalSpy done(&checker,&UpdateChecker::finished);
  checker.check("1.3.0-alpha.1",true);
  QCOMPARE(done.size(),1);
  QCOMPARE(qvariant_cast<UpdateResult>(done[0][0]).messageKey,QString("update.alpha_development_only"));
  QVERIFY(manager.requests.isEmpty()); QVERIFY(!checker.busy());
  // Even a mistakenly published Alpha marked as a stable release is excluded.
  const auto page=parseReleasePage(json({release("v99.0.0-alpha.1",false),release("v1.3.0-beta.14",true)}),config(),true);
  QVERIFY(page.errorKey.isEmpty()); QCOMPARE(page.releases.size(),1);
  QCOMPARE(page.releases.first().tag,QString("v1.3.0-beta.14"));
 }
 void compiledVersionMatchesSettingsAndHelp() {
  QCOMPARE(QCoreApplication::applicationVersion(), QStringLiteral(ASTER_VERSION));
  aster::ui::SettingsDialog dialog({});
  const auto expected = aster::ui::text("update.installed") + "  " + QCoreApplication::applicationVersion();
  bool found = false;
  for (auto *label : dialog.findChildren<QLabel*>()) found |= label->text() == expected;
  QVERIFY2(found, "Settings must show the full compiled version");
  QFile help(":/help/FAQ.md");
  QVERIFY(help.open(QIODevice::ReadOnly));
  QVERIFY2(help.readAll().contains(ASTER_VERSION), "Rebuild embedded help for the current version");
 }
 void languageRestartDeferredAndAccepted_data() {
  QTest::addColumn<QString>("before");QTest::addColumn<QString>("after");
  QTest::newRow("zh-en")<<QString("zh")<<QString("en");
  QTest::newRow("en-zh")<<QString("en")<<QString("zh");
 }
 void languageRestartDeferredAndAccepted() {
  QFETCH(QString,before);QFETCH(QString,after);aster::ui::setLanguage(before);
  QTemporaryDir dir;aster::ui::AppSettings settings;settings.language=before;settings.databasePath=dir.filePath("language.sqlite");
  aster::ui::MainWindow window(settings,false);window.show();
  QVERIFY(window.document()->addRoot("sine_source").ok());
  auto edit=editLanguage(window,after,QMessageBox::No);
  QVERIFY(edit.submitted && edit.prompted && edit.workflowNotice && !edit.timedOut);
  QCOMPARE(window.settings().language,after);QVERIFY(!window.restartRequested());QVERIFY(window.isVisible());
  QCOMPARE(aster::ui::LanguageCatalog::instance().language(),before);
  QCOMPARE(window.document()->model().definition().nodes.size(),1);
  edit=editLanguage(window,after,QMessageBox::Yes);
  QVERIFY(edit.prompted && !edit.timedOut);
  QTRY_VERIFY(!window.isVisible());QVERIFY(window.restartRequested());
  aster::ui::setLanguage("zh");
 }
 void unchangedOrCancelledLanguageDoesNotRestart() {
  aster::ui::setLanguage("zh");QTemporaryDir dir;aster::ui::AppSettings settings;settings.databasePath=dir.filePath("unchanged.sqlite");
  aster::ui::MainWindow window(settings,false);window.show();
  auto edit=editLanguage(window,"zh",QMessageBox::Yes);
  QVERIFY(edit.submitted && !edit.prompted && !edit.timedOut);QVERIFY(!window.restartRequested());QVERIFY(window.isVisible());
  edit=editLanguage(window,"en",QMessageBox::Yes,true);
  QVERIFY(edit.submitted && !edit.prompted && !edit.timedOut);QCOMPARE(window.settings().language,QString("zh"));
  QVERIFY(!window.restartRequested());QVERIFY(window.isVisible());
 }
 void cancelledCloseClearsRestartRequest() {
  aster::ui::setLanguage("zh");QTemporaryDir dir;aster::ui::AppSettings settings;settings.databasePath=dir.filePath("unsaved.sqlite");
  aster::ui::MainWindow window(settings,false);window.show();
  auto result=std::make_shared<aster::RunResult>();result->runId=QUuid::createUuid();
  auto batch=std::make_shared<aster::BatchResult>();aster::BatchItem item;item.runId=result->runId;item.unsavedResult=result;batch->items.append(item);
  window.runner()->batchReady(batch);
  QTimer answerClose;bool declined=false;bool allow=false;bool timedOut=false;
  QElapsedTimer closeElapsed;closeElapsed.start();
  connect(&answerClose,&QTimer::timeout,&window,[&] {
   auto *box=qobject_cast<QMessageBox*>(qApp->activeModalWidget());
   if(box && box->parentWidget()==&window &&
      box->text()==aster::ui::text("ui.some_results_have_not_been_stored_closing_discards_these_in_")) {
    // QMessageBox ignores titles on macOS. Exercise that condition on every
    // host and identify the confirmation by its parent and message instead.
    box->QWidget::setWindowTitle(QString());
    if(!allow)declined=true;
    box->button(allow?QMessageBox::Yes:QMessageBox::No)->click();
   } else if(closeElapsed.elapsed()>10000) {
    timedOut=true;
    if(auto *dialog=qobject_cast<QDialog*>(qApp->activeModalWidget()))dialog->reject();
   }
  });answerClose.start(5);
  const auto edit=editLanguage(window,"en",QMessageBox::Yes);QVERIFY(edit.prompted && !edit.timedOut);
  QTRY_VERIFY(declined);QVERIFY(!timedOut);QVERIFY(window.isVisible());QVERIFY(!window.restartRequested());
  allow=true;closeElapsed.restart();window.close();QTRY_VERIFY(!window.isVisible());QVERIFY(!timedOut);
  QVERIFY(!window.restartRequested());
 }
 void numericTextInsetSurvivesStyleChanges() {
  for(auto theme:{"light","dark"}) {
   aster::ui::ThemeManager::instance().apply(*qApp,theme);
   aster::ui::SettingsDialog dialog({});dialog.resize(680,520);dialog.show();
   auto *spin=dialog.findChild<QSpinBox*>("settingsConcurrency");
   auto *combo=dialog.findChild<QComboBox*>("settingsTheme");
   auto *edit=spin->findChild<QLineEdit*>();QVERIFY(edit);
   for(bool frameEdge:{false,true,false}) {
    // Reproduce a style that gives the internal editor no left padding.
    spin->setStyleSheet(frameEdge ? "QSpinBox { padding-left: 0px; }" : "");
    QTest::qWait(10);
    QStyleOptionComboBox option;option.initFrom(combo);
    const auto comboText=combo->style()->subControlRect(QStyle::CC_ComboBox,&option,QStyle::SC_ComboBoxEditField,combo).left();
    const int spinText=edit->geometry().left()+edit->textMargins().left()+2;
    QVERIFY2(spinText>=comboText && spinText<=comboText+2,qPrintable(QString("spin=%1 combo=%2").arg(spinText).arg(comboText)));
    spin->setValue(12);QTest::keyClick(spin,Qt::Key_Up);QCOMPARE(spin->value(),13);
   }
  }
 }
 void helpColumnsFollowViewport() {
  for(auto theme:{"light","dark"}) for(auto lang:{"zh","en"}) {
   aster::ui::ThemeManager::instance().apply(*qApp,theme);aster::ui::setLanguage(lang);
   const auto registry=aster::makeScientificRegistry();
   aster::ui::ParameterEditor editor;editor.show();
   for(const auto method:{"odp1143_d18o","sine_source","mtm"}) {
    aster::NodeDefinition node;node.id=QUuid::createUuid();node.method=method;
    QVERIFY(registry.find(method));editor.showNode(&node,registry);
    auto *title=editor.findChild<QToolButton*>("methodHelp");QVERIFY(title);
    if(node.method!="mtm")QVERIFY(!title->isHidden());
    for(const auto size:{QSize(280,220),QSize(390,2200),QSize(280,220)}) {
     editor.resize(size);
     QString diagnostic;
     const auto aligned = [&] {
      int hints=0;
      int column=title->isHidden() ? -1 : title->mapTo(&editor,QPoint(title->width()/2,0)).x();
      for(auto *hint:editor.findChildren<QToolButton*>("ParameterHelp")) {
       if(!hint->isVisible())continue;
       const int x=hint->mapTo(&editor,QPoint(hint->width()/2,0)).x();
       if(column<0)column=x;
       if(std::abs(x-column)>1) {
        diagnostic=QString("%1 %2 %3 %4x%5 dx=%6")
          .arg(theme,lang,method).arg(size.width()).arg(size.height()).arg(x-column);
        return false;
       }
       ++hints;
      }
      diagnostic="no visible parameter help";
      return hints>0;
     };
     QTRY_VERIFY2_WITH_TIMEOUT(aligned(),qPrintable(diagnostic),2000);
    }
   }
  }
  aster::ui::setLanguage("zh");aster::ui::ThemeManager::instance().apply(*qApp,"light");
 }
 void helpColumnTracksFormResize() {
  aster::ui::setLanguage("zh");
  const auto registry=aster::makeScientificRegistry();
  aster::ui::ParameterEditor editor;
  aster::NodeDefinition node;node.id=QUuid::createUuid();node.method="sine_source";
  editor.resize(360,500);editor.showNode(&node,registry);editor.show();
  auto *scroll=editor.findChild<QScrollArea *>();
  auto *title=editor.findChild<QToolButton *>("methodHelp");
  auto *hint=editor.findChild<QToolButton *>("ParameterHelp");
  QVERIFY(scroll && title && hint);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  QWidget *form=scroll->widget();
  QTRY_COMPARE(form->width(),scroll->viewport()->width());
  const int viewportWidth=scroll->viewport()->width();
  const auto center=[&editor](QToolButton *button) {
   return button->mapTo(&editor,QPoint(button->width()/2,0)).x();
  };
  // Isolate the geometry change that a viewport-only listener misses.
  // Both help buttons must follow the form even at a fixed viewport width.
  for(int reduction:{8,0}) {
   form->setMaximumWidth(viewportWidth-reduction);
   QTRY_COMPARE(form->width(),viewportWidth-reduction);
   QCOMPARE(scroll->viewport()->width(),viewportWidth);
   QTRY_VERIFY(std::abs(center(title)-center(hint))<=1);
  }
 }
 void settingsValidationFreezesFields() {
  QTemporaryDir dir;aster::ui::AppSettings values;values.databasePath=dir.filePath("checked.sqlite");values.concurrency=2;
  aster::ui::SettingsDialog dialog(values);dialog.show();
  auto *spin=dialog.findChild<QSpinBox*>("settingsConcurrency");auto *language=dialog.findChild<QComboBox*>("settingsLanguage");auto *theme=dialog.findChild<QComboBox*>("settingsTheme");
  spin->setValue(12);language->setCurrentIndex(language->findData("en"));theme->setCurrentIndex(theme->findData("dark"));
  QSignalSpy accepted(&dialog,&QDialog::accepted);
  dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
  QVERIFY(!spin->isEnabled());QVERIFY(!language->isEnabled());QVERIFY(!theme->isEnabled());
  QTRY_COMPARE(accepted.size(),1);
  QCOMPARE(dialog.values().concurrency,12);QCOMPARE(dialog.values().language,QString("en"));QCOMPARE(dialog.values().theme,QString("dark"));
  QVERIFY(spin->isEnabled() && language->isEnabled() && theme->isEnabled());
 }
 void closedSettingsIgnorePendingValidation() {
  QTemporaryDir dir;aster::ui::AppSettings values;values.databasePath=dir.filePath("pending.sqlite");
  aster::ui::SettingsDialog dialog(values);dialog.show();QSignalSpy accepted(&dialog,&QDialog::accepted);
  dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();dialog.reject();QTest::qWait(100);
  QCOMPARE(accepted.size(),0);QCOMPARE(dialog.result(),int(QDialog::Rejected));
 }
 void concurrencySettingsPersistence() {
  QTemporaryDir dir;
  const auto path=dir.filePath("settings.ini");
  const auto fresh=aster::ui::AppSettings::load(path);
  QCOMPARE(fresh.concurrency,aster::defaultConcurrency());
  QVERIFY(fresh.concurrency>=1 && fresh.concurrency<=8);
  for(int value:{2,8,12,16,32}) {
   auto settings=fresh;settings.concurrency=value;
   QVERIFY(settings.save(path).isEmpty());
   QCOMPARE(aster::ui::AppSettings::load(path).concurrency,value);
  }
  {QSettings s(path,QSettings::IniFormat);s.setValue("execution/concurrency","invalid");}
  QCOMPARE(aster::ui::AppSettings::load(path).concurrency,fresh.concurrency);
  {QSettings s(path,QSettings::IniFormat);s.setValue("execution/concurrency",100);}
  QCOMPARE(aster::ui::AppSettings::load(path).concurrency,aster::maximumConcurrency);
 }
 void settingsLayoutAndQuietUpdate() {
  for(auto theme:{"light","dark"}) for(auto lang:{"zh","en"}) {
   aster::ui::ThemeManager::instance().apply(*qApp,theme);
   aster::ui::setLanguage(lang);
   aster::ui::SettingsDialog dialog({});dialog.show();QTest::qWait(5);
   auto *spin=dialog.findChild<QSpinBox*>("settingsConcurrency");
   auto *language=dialog.findChild<QComboBox*>("settingsLanguage");
   auto *palette=dialog.findChild<QComboBox*>("settingsTheme");
   QVERIFY(spin && language && palette);
   QCOMPARE(spin->value(),aster::defaultConcurrency());
   QCOMPARE(spin->geometry().left(),language->geometry().left());
   QCOMPARE(spin->geometry().right(),language->geometry().right());
   QCOMPARE(spin->geometry().left(),palette->geometry().left());
   auto *help=dialog.findChild<QToolButton*>("ParameterHelp");
   QVERIFY(help && !help->toolTip().isEmpty());
   QVERIFY(help->mapTo(&dialog,QPoint()).y()<language->geometry().top());
   aster::ui::UpdateDialog updates;
   QVERIFY(!updates.findChild<QLabel*>("updateStatus")->text().isEmpty());
   auto *notes=updates.findChild<QTextBrowser*>("updateNotes");
   QVERIFY(notes);
   QVERIFY(notes->isReadOnly());
   QVERIFY(!updates.findChild<UpdateChecker*>()->busy());
  }
  aster::ui::setLanguage("zh");
  aster::ui::ThemeManager::instance().apply(*qApp,"light");
 }
 void methodHelpFollowsSelection() {
  for(auto lang:{"zh","en"}) {
   aster::ui::setLanguage(lang);
   aster::ui::WorkflowDocument doc(aster::makeScientificRegistry());
   const auto odp=doc.addRoot("odp1143_d18o").nodeId;
   aster::ui::ParameterEditor editor;
   editor.showNode(doc.node(odp),doc.model().registry());
   auto *help=editor.findChild<QToolButton*>("methodHelp");
   QVERIFY(help && !help->isHidden());
   QVERIFY(help->toolTip().isEmpty());
   QVERIFY(help->accessibleDescription().contains("PANGAEA.132801"));
   QVERIFY(help->accessibleDescription().contains("CC BY 3.0"));
   help->click();
   auto *dialog=editor.findChild<aster::ui::HelpDialog*>("helpDialog");
   QVERIFY(dialog && dialog->isVisible());
   QCOMPARE(dialog->currentTopic().anchor,QString("m-odp1143_d18o"));
   auto *browser=dialog->findChild<QTextBrowser*>("helpBrowser");
   QVERIFY(browser);
   QVERIFY(browser->toPlainText().contains("PANGAEA.132801"));
   QVERIFY(browser->toPlainText().contains("CC BY 3.0"));
   dialog->close();
   for(auto *label:editor.findChildren<QLabel*>())
    QVERIFY(!label->text().contains("PANGAEA.132801"));
   editor.showNode(nullptr,doc.model().registry());
   QVERIFY(help->isHidden());QVERIFY(help->toolTip().isEmpty());
   QVERIFY(help->accessibleDescription().isEmpty());
   const auto sine=doc.addRoot("sine_source").nodeId;
   editor.showNode(doc.node(sine),doc.model().registry());
   QVERIFY(!help->isHidden());QVERIFY(help->toolTip().isEmpty());
   QVERIFY(!help->accessibleDescription().isEmpty());
   QVERIFY(!help->accessibleDescription().contains("PANGAEA"));
   help->click();
   QCOMPARE(dialog->currentTopic().anchor,QString("m-sine_source"));
   dialog->close();
   auto invalid=*doc.node(sine);invalid.method="unregistered_method";
   editor.showNode(&invalid,doc.model().registry());
   QVERIFY(help->isHidden());QVERIFY(help->toolTip().isEmpty());
   QVERIFY(help->accessibleDescription().isEmpty());
  }
  aster::ui::setLanguage("zh");
 }
 void initTestCase(){QCoreApplication::setApplicationVersion(ASTER_VERSION);aster::ui::setLanguage("zh");qRegisterMetaType<UpdateResult>();}
 void versionOrder_data(){QTest::addColumn<QString>("a");QTest::addColumn<QString>("b");QTest::addColumn<int>("sign");
  QTest::newRow("stable")<<"1.0.0"<<"1.0.1"<<-1;
  QTest::newRow("two-digit")<<"1.0.9"<<"1.0.10"<<-1;
  QTest::newRow("stable-after-beta")<<"1.0.0"<<"1.0.0-beta.38"<<1;
  QTest::newRow("numeric-beta")<<"1.0.0-beta.9"<<"1.0.0-beta.10"<<-1;
  QTest::newRow("numeric-before-word")<<"1.0.0-1"<<"1.0.0-alpha"<<-1;
  QTest::newRow("prefix")<<"v1.0.0"<<"1.0.0+build.4"<<0;
  QTest::newRow("large-major")<<"10000000000000000000000.0.0"<<"9999999999999999999999.0.0"<<1;
  QTest::newRow("extra-prerelease")<<"1.0.0-beta"<<"1.0.0-beta.1"<<-1;
 }
 void versionOrder(){QFETCH(QString,a);QFETCH(QString,b);QFETCH(int,sign);auto x=ReleaseVersion::parse(a),y=ReleaseVersion::parse(b);QVERIFY(x&&y);const auto c=x->compare(*y);QCOMPARE(c==0?0:c<0?-1:1,sign);}
 void invalidVersions_data(){QTest::addColumn<QString>("s");for(auto s:{"1.0","01.0.0","1.0.0-beta.01","1.0.0-","1.0.0+","1.0.0/evil"," 1.0.0","1.0.0\n","1.0.0-é","V1.0.0"})QTest::newRow(s)<<QString::fromUtf8(s);}
 void invalidVersions(){QFETCH(QString,s);QVERIFY(!ReleaseVersion::parse(s));}
 void selectReleases(){auto c=config();auto p=parseReleasePage(json({release("1.0.9"),release("v1.0.10"),release("9.0.0",false,true),release("3.0.0-beta.1"),release("2.0.0",true)}),c,false);
  QVERIFY(p.errorKey.isEmpty());QCOMPARE(newestRelease(p.releases)->tag,QString("v1.0.10"));
  p=parseReleasePage(json({release("v1.0.1"),release("2.0.0-beta.2",true)}),c,true);QCOMPARE(newestRelease(p.releases)->tag,QString("2.0.0-beta.2"));
  auto o=release("v1.0.1");o["html_url"]="file:///etc/passwd";o["body"]="<script>bad</script>";
  p=parseReleasePage(json({o}),c,false);QCOMPARE(p.releases[0].pageUrl,QUrl("https://github.com/ChenCLian/AstroBatch/releases/tag/v1.0.1"));
  QVERIFY(!parseReleasePage("{}",c,false).errorKey.isEmpty());QVERIFY(!parseReleasePage("[{}]",c,false).errorKey.isEmpty());
 }
 void responses_data(){QTest::addColumn<int>("status");QTest::addColumn<QByteArray>("body");QTest::addColumn<QString>("key");
  QTest::newRow("empty")<<200<<QByteArray("[]")<<"update.none";
  QTest::newRow("update")<<200<<json({release("v1.0.1")})<<"update.available";
  QTest::newRow("current")<<200<<json({release("v1.0.0")})<<"update.current";
  QTest::newRow("older")<<200<<json({release("v0.9.0")})<<"update.current";
  QTest::newRow("repository")<<404<<QByteArray("{}")<<"update.error_repository";
  QTest::newRow("unauthorized")<<401<<QByteArray("{}")<<"update.error_permission";
  QTest::newRow("permission")<<403<<QByteArray("{}")<<"update.error_permission";
  QTest::newRow("rate")<<429<<QByteArray("{}")<<"update.error_rate";
  QTest::newRow("server")<<500<<QByteArray("{}")<<"update.error_server";
  QTest::newRow("redirect")<<302<<QByteArray("[]")<<"update.error_redirect";
  QTest::newRow("malformed")<<200<<QByteArray("oops")<<"update.error_response";
 }
 void responses(){QFETCH(int,status);QFETCH(QByteArray,body);QFETCH(QString,key);FakeManager m;m.responses.append({status,body});UpdateChecker c(config(),nullptr,&m);QSignalSpy done(&c,&UpdateChecker::finished);c.check("1.0.0",false);QVERIFY(done.wait());QCOMPARE(done.size(),1);QVERIFY(!c.busy());auto r=qvariant_cast<UpdateResult>(done[0][0]);QCOMPARE(r.messageKey,key);
  QCOMPARE(m.requests[0].url(),config().apiUrl(1));QVERIFY(!m.requests[0].hasRawHeader("Authorization"));
  m.responses.append({200,"[]"});done.clear();c.check("1.0.0");QVERIFY(done.wait());QVERIFY(m.requests[1].rawHeader("Authorization").isEmpty());
 }
 void cancellationAndRetry(){FakeManager m;m.responses={{200,"[]",100},{200,json({release("v1.0.1")}),1}};UpdateChecker c(config(),nullptr,&m);QSignalSpy done(&c,&UpdateChecker::finished);c.check("1.0.0");c.check("1.0.0");QCOMPARE(m.requests.size(),1);c.cancel();QCOMPARE(done.size(),1);QCOMPARE(qvariant_cast<UpdateResult>(done[0][0]).state,UpdateResult::State::Cancelled);done.clear();c.check("1.0.0");QVERIFY(done.wait());QCOMPARE(qvariant_cast<UpdateResult>(done[0][0]).state,UpdateResult::State::Available);QTest::qWait(120);QCOMPARE(done.size(),1);}
 void timeoutAndLimits(){auto cfg=config();cfg.timeoutMs=15;FakeManager m;m.responses={{200,"[]",200}};UpdateChecker c(cfg,nullptr,&m);QSignalSpy done(&c,&UpdateChecker::finished);c.check("1.0.0");QVERIFY(done.wait());QCOMPARE(qvariant_cast<UpdateResult>(done[0][0]).messageKey,QString("update.error_timeout"));
  cfg.maximumResponseBytes=64;FakeManager oversized;oversized.responses={{200,QByteArray(65,'x')}};UpdateChecker limited(cfg,nullptr,&oversized);QSignalSpy large(&limited,&UpdateChecker::finished);limited.check("1.0.0");QVERIFY(large.wait());QCOMPARE(qvariant_cast<UpdateResult>(large[0][0]).messageKey,QString("update.error_response_limit"));
 }
 void pagination(){QJsonArray first;for(int i=0;i<100;++i)first.append(release("v1.0.0"));FakeManager m;m.responses={{200,json(first)},{200,json({release("v1.0.3")})}};UpdateChecker c(config(),nullptr,&m);QSignalSpy done(&c,&UpdateChecker::finished);c.check("1.0.0");QVERIFY(done.wait());QCOMPARE(m.requests.size(),2);QCOMPARE(m.requests[1].url(),config().apiUrl(2));QCOMPARE(qvariant_cast<UpdateResult>(done[0][0]).release->tag,QString("v1.0.3"));
  auto cfg=config();cfg.maximumPages=1;FakeManager cap;cap.responses={{200,json(first)}};UpdateChecker limited(cfg,nullptr,&cap);QSignalSpy last(&limited,&UpdateChecker::finished);limited.check("1.0.0");QVERIFY(last.wait());QCOMPARE(qvariant_cast<UpdateResult>(last[0][0]).messageKey,QString("update.error_page_limit"));
 }
 void destructionCancels(){FakeManager m;m.responses={{200,"[]",50}};int completions=0;auto *c=new UpdateChecker(config(),nullptr,&m);connect(c,&UpdateChecker::finished,this,[&]{++completions;});c->check("1.0.0");delete c;QTest::qWait(70);QCOMPARE(completions,0);}
 void realAsyncHttp(){QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));QByteArray received;const auto payload=json({release("v1.0.1")});
  connect(&server,&QTcpServer::newConnection,this,[&]{auto *s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[&,s]{received+=s->readAll();if(!received.contains("\r\n\r\n"))return;s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\n\r\n"+payload);s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);});
  LoopbackManager m;m.port=server.serverPort();UpdateChecker c(config(),nullptr,&m);QSignalSpy done(&c,&UpdateChecker::finished);c.check("1.0.0");QVERIFY(done.wait());QCOMPARE(qvariant_cast<UpdateResult>(done[0][0]).state,UpdateResult::State::Available);QCOMPARE(m.requested,config().apiUrl(1));QVERIFY(received.contains("/repos/ChenCLian/AstroBatch/releases?per_page=100&page=1"));QVERIFY(!received.toLower().contains("authorization:"));
 }
 void updateWindowStatesAndChannels(){
  const auto version = useApplicationVersion("1.3.0-beta.1");
  FakeManager manager;
  manager.responses={{500,"{}",1},{200,json({release("v99.0.0",false),release("v99.0.0-beta.2",true),release("v100.0.0-rc.1",true),release("v200.0.0-alpha.1",true)}),1},{200,json({release("v1.0.0",false),release("v99.0.0-beta.2",true)}),1}};
  aster::ui::UpdateDialog dialog(config(),nullptr,&manager);dialog.show();
  auto *client=dialog.findChild<UpdateChecker*>();auto *tabs=dialog.findChild<QTabBar*>("updateChannel");
  auto *primary=dialog.findChild<QPushButton*>("updateOpen");auto *details=dialog.findChild<QToolButton*>("updateDetails");
  QVERIFY(client && tabs && primary && details);QCOMPARE(tabs->currentIndex(),1);
  QTRY_COMPARE(manager.requests.size(),1);QTRY_VERIFY(!client->busy());
  QCOMPARE(dialog.findChild<QFrame*>("updateSummary")->property("state").toString(),QString("error"));
  QVERIFY(primary->isEnabled());QVERIFY(details->isVisible());
  details->click();QVERIFY(dialog.findChild<QLabel*>("updateDiagnostics")->isVisible());
  QVERIFY(dialog.findChild<QLabel*>("updateDiagnostics")->text().contains("update.error_server"));
  primary->click();QTRY_COMPARE(manager.requests.size(),2);QTRY_VERIFY(!client->busy());
  QCOMPARE(dialog.findChild<QFrame*>("updateSummary")->property("state").toString(),QString("available"));
  QVERIFY(dialog.findChild<QLabel*>("updateStatus")->text().contains("99.0.0-beta.2"));
  QVERIFY(!details->isVisible());QVERIFY(!dialog.findChild<QLabel*>("updateDiagnostics")->isVisible());
  tabs->setCurrentIndex(0);QTRY_COMPARE(manager.requests.size(),3);QTRY_VERIFY(!client->busy());
  QCOMPARE(dialog.findChild<QFrame*>("updateSummary")->property("state").toString(),QString("current"));
  QVERIFY(!primary->isEnabled());QVERIFY(dialog.findChild<QLabel*>("updateReleaseMeta")->text().contains("1.0.0"));
  dialog.reject();
 }
 void updateWindowCancellationAndClose(){
  const auto version = useApplicationVersion("1.3.0-beta.1");
  FakeManager manager;manager.responses={{200,"[]",10000},{200,"[]",10000}};
  aster::ui::UpdateDialog dialog(config(),nullptr,&manager);dialog.show();
  auto *client=dialog.findChild<UpdateChecker*>();
  QTRY_VERIFY(client->busy());QCOMPARE(manager.requests.size(),1);
  dialog.findChild<QPushButton*>("updateCancel")->click();QVERIFY(!client->busy());
  QCOMPARE(dialog.findChild<QFrame*>("updateSummary")->property("state").toString(),QString("cancelled"));
  dialog.findChild<QPushButton*>("updateOpen")->click();QVERIFY(client->busy());
  dialog.reject();QVERIFY(!client->busy());QTest::qWait(20);QCOMPARE(manager.requests.size(),2);
  FakeManager closedManager;aster::ui::UpdateDialog closed(config(),nullptr,&closedManager);
  closed.show();closed.reject();QTest::qWait(20);QVERIFY(closedManager.requests.isEmpty());
 }
 void updateWindowEmptyAndTimeout(){
  const auto version = useApplicationVersion("1.3.0-beta.1");
  FakeManager manager;manager.responses={{200,"[]",1},{200,"[]",10000}};
  auto cfg=config();cfg.timeoutMs=25;aster::ui::UpdateDialog dialog(cfg,nullptr,&manager);dialog.show();
  auto *client=dialog.findChild<UpdateChecker*>();
  QTRY_COMPARE(manager.requests.size(),1);QTRY_VERIFY(!client->busy());
  QCOMPARE(dialog.findChild<QFrame*>("updateSummary")->property("state").toString(),QString("empty"));
  QVERIFY(!dialog.findChild<QPushButton*>("updateOpen")->isEnabled());
  dialog.findChild<QPushButton*>("updateCheck")->click();QTRY_VERIFY(!client->busy());
  QCOMPARE(dialog.findChild<QFrame*>("updateSummary")->property("state").toString(),QString("error"));
  QCOMPARE(dialog.findChild<QLabel*>("updateStatus")->text(),aster::ui::text("update.error_timeout"));
  dialog.reject();
 }
 void updateWindowAlphaStaysOffline(){
  const auto version = useApplicationVersion("1.3.0-alpha.99");
  FakeManager manager;
  aster::ui::UpdateDialog dialog(config(),nullptr,&manager);dialog.show();QTest::qWait(20);
  QVERIFY(manager.requests.isEmpty());QVERIFY(!dialog.findChild<QPushButton*>("updateCheck")->isEnabled());
  QVERIFY(!dialog.findChild<QTabBar*>("updateChannel")->isEnabled());
  QCOMPARE(dialog.findChild<QFrame*>("updateSummary")->property("state").toString(),QString("development"));
  dialog.reject();
 }
 void updateWindowVisuals_data(){
  QTest::addColumn<QString>("language");QTest::addColumn<QString>("theme");QTest::addColumn<int>("width");
  for(const auto &language:{QString("zh"),QString("en")})for(const auto &theme:{QString("light"),QString("dark")})for(int width:{660,520})
   QTest::newRow(qPrintable(language+"-"+theme+"-"+QString::number(width)))<<language<<theme<<width;
 }
 void updateWindowVisuals(){
  const auto version = useApplicationVersion("1.3.0-beta.1");
  QFETCH(QString,language);QFETCH(QString,theme);QFETCH(int,width);
  auto restore=qScopeGuard([&]{aster::ui::setLanguage("zh");aster::ui::ThemeManager::instance().apply(*qApp,"light");});
  aster::ui::setLanguage(language);aster::ui::ThemeManager::instance().apply(*qApp,theme);
  auto item=release("v1.4.0-beta.1",true);item["published_at"]="2026-11-17T08:30:00Z";
  item["body"]=language=="zh"?QString::fromUtf8("## 本次更新\n\n优化参数设置与批量处理，改善结果预览的操作体验。\n\n- 调整步骤设置中的提示与布局\n- 改善长文件名的显示\n- 保留原有数据与分析流程\n\n<details><summary>兼容性与使用说明</summary>\n\nDETAIL_BODY\n\n请将完整程序包解压到新的文件夹。\n</details>"):
   QString("## What's new\n\nImproved parameter editing, batch processing and result previews.\n\n- Clearer step settings and guidance\n- Better display of long file names\n- Existing data and workflows are preserved\n\n<details><summary>Compatibility and usage</summary>\n\nDETAIL_BODY\n\nExtract the complete package into a new folder.\n</details>");
  FakeManager manager;manager.responses={{200,json({item}),1},{500,"{}",1}};
  aster::ui::UpdateDialog dialog(config(),nullptr,&manager);dialog.resize(width,580);dialog.show();
  auto *client=dialog.findChild<UpdateChecker*>();QTRY_COMPARE(manager.requests.size(),1);QTRY_VERIFY(!client->busy());QTest::qWait(20);
  QCOMPARE(dialog.width(),width);
  auto *notes=dialog.findChild<QTextBrowser*>("updateNotes");QVERIFY(notes);
  QVERIFY(!notes->toPlainText().contains("DETAIL_BODY"));
  QVERIFY(QMetaObject::invokeMethod(notes,"anchorClicked",Qt::DirectConnection,Q_ARG(QUrl,QUrl("astrobatch-note:1"))));
  QVERIFY(notes->toPlainText().contains("DETAIL_BODY"));
  QVERIFY(QMetaObject::invokeMethod(notes,"anchorClicked",Qt::DirectConnection,Q_ARG(QUrl,QUrl("astrobatch-note:1"))));
  QVERIFY(!notes->toPlainText().contains("DETAIL_BODY"));
  QVERIFY(!notes->document()->resource(QTextDocument::ImageResource,QUrl("file:///etc/passwd")).isValid());
  QVERIFY(!notes->document()->resource(QTextDocument::ImageResource,QUrl("https://example.invalid/image.png")).isValid());
  QVERIFY(notes->height()>=90);QVERIFY(notes->horizontalScrollBar()->maximum()==0);
  for(const auto *name:{"updateHeading","updateStatus","updateManualHint","updateOpen","updateClose","updateReleases"}) {
   auto *widget=dialog.findChild<QWidget*>(name);QVERIFY(widget);
   const QRect bounds(widget->mapTo(&dialog,QPoint()),widget->size());
   QVERIFY2(dialog.rect().contains(bounds),name);
  }
  const auto screenshots=qEnvironmentVariable("ASTER_UPDATE_SCREENSHOT_DIR");
  if(!screenshots.isEmpty()) {
   QVERIFY(QDir().mkpath(screenshots));
   const auto stem=language+"-"+theme+"-"+QString::number(width);
   QVERIFY(dialog.grab().save(screenshots+"/"+stem+"-available.png"));
   dialog.findChild<QPushButton*>("updateCheck")->click();QTRY_VERIFY(!client->busy());
   dialog.findChild<QToolButton*>("updateDetails")->click();QTest::qWait(10);
   QCOMPARE(dialog.width(),width);
   QVERIFY(dialog.grab().save(screenshots+"/"+stem+"-error.png"));
  }
  dialog.reject();
 }
 void packageDownloadWindow_data(){
  QTest::addColumn<QString>("language");
  QTest::addColumn<bool>("mirrored");
  QTest::addColumn<bool>("transferFallback");
  for(const QString &language:QStringList{"zh","en"}){
   QTest::newRow(qPrintable(language+"-single"))<<language<<false<<false;
   QTest::newRow(qPrintable(language+"-mirror"))<<language<<true<<false;
   QTest::newRow(qPrintable(language+"-archive-fallback"))<<language<<true<<true;
  }
 }
 void packageDownloadWindow(){
  const auto version = useApplicationVersion("1.3.0-beta.1");
  QFETCH(bool,mirrored);QFETCH(bool,transferFallback);QFETCH(QString,language);aster::ui::setLanguage(language);
  const auto theme=language=="zh"?"light":"dark";aster::ui::ThemeManager::instance().apply(*qApp,theme);
  const bool previous=QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
  auto restore=qScopeGuard([&]{QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,previous);aster::ui::setLanguage("zh");aster::ui::ThemeManager::instance().apply(*qApp,"light");});
  const auto platform=aster::updates::nativeUpdatePlatform();const auto suffix=platform=="linux-x64"?".tar.gz":".zip";
  const auto fixture=QString("../download_tests/fixtures/")+platform+suffix;
  QFile file(QFINDTESTDATA(qPrintable(fixture)));QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();
  const auto name=QString("AstroBatch-99.0.0-beta.1-")+platform+suffix;
  QJsonObject package{{"platform",platform},{"file",name},{"bytes",bytes.size()},{"unpacked_bytes",4096},{"sha256",QString(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},{"sources",QJsonArray{QJsonObject{{"name","GitHub"},{"url","https://github.com/ChenCLian/AstroBatch/releases/download/v99.0.0-beta.1/"+name}}}}};
  auto sources=package["sources"].toArray();
  if(mirrored)sources.append(QJsonObject{{"name","Gitee"},{"url","https://gitee.com/ChenCLian/AstroBatch/releases/download/v99.0.0-beta.1/"+name}});
  package["sources"]=sources;
  const auto manifest=QJsonDocument(QJsonObject{{"schema",1},{"version","99.0.0-beta.1"},{"packages",QJsonArray{package}}}).toJson();
  auto cfg=config();cfg.packageMetadata="AstroBatch-update.json";cfg.mirrorFallback=mirrored;
  FakeManager manager;
  if(transferFallback)manager.responses.append(ReplyData{503,{}});
  manager.responses.append(ReplyData{200,json({release("v99.0.0-beta.1",true)})});
  if(mirrored){
   if(!transferFallback)manager.responses.append(ReplyData{503,{}});
   const QString notes="Release notes\n\n<!-- astrobatch-update-v1\n"+QString::fromUtf8(manifest)+"\n-->";
   manager.responses.append(ReplyData{200,QJsonDocument(QJsonObject{{"tag_name","v99.0.0-beta.1"},{"body",notes}}).toJson()});
  }else manager.responses.append(ReplyData{200,manifest});
  if(transferFallback)manager.responses.append(ReplyData{503,{}});
  manager.responses.append(ReplyData{200,bytes});
  const QString notesSite=transferFallback?"Gitee":"GitHub";
  aster::ui::UpdateDialog dialog(cfg,nullptr,&manager);if(language=="en")dialog.resize(520,580);dialog.show();
  auto *downloader=dialog.findChild<aster::updates::PackageDownload*>();QVERIFY(downloader);
  QTRY_COMPARE(manager.requests.size(),mirrored?3:2);QTRY_VERIFY(!downloader->busy());
  auto *button=dialog.findChild<QPushButton*>("updateOpen");
  auto *pickerSource=dialog.findChild<QComboBox*>("updateDownloadSource");
  auto *onlySource=dialog.findChild<QLabel*>("updateOnlySource");
  auto *announcement=dialog.findChild<QLabel*>("updateSourceLabel");
  QCOMPARE(button->text(),aster::ui::text("update.download_extract"));
  QCOMPARE(announcement->text(),aster::ui::text("update.announcement_source").arg(notesSite));
  QCOMPARE(pickerSource->isVisible(),mirrored);QCOMPARE(onlySource->isVisible(),!mirrored);
  if(mirrored){
   QCOMPARE(pickerSource->count(),3);QCOMPARE(pickerSource->currentData().toString(),QString("auto"));
   QCOMPARE(pickerSource->currentText(),aster::ui::text("update.source_auto").arg("Gitee"));
   QVERIFY(pickerSource->toolTip().contains("Gitee"));
  }
  else QCOMPARE(onlySource->text(),QString("GitHub"));
  QTemporaryDir folder;QElapsedTimer timer;timer.start();QTimer picker;bool selected=false,timeout=false;
  connect(&picker,&QTimer::timeout,&dialog,[&]{
   if(auto *chooser=dialog.findChild<QFileDialog*>();chooser&&chooser->isVisible()){chooser->setDirectory(folder.path());QMetaObject::invokeMethod(chooser,"accept",Qt::DirectConnection);selected=true;picker.stop();}
   else if(timer.elapsed()>5000){timeout=true;picker.stop();if(auto *modal=qobject_cast<QDialog*>(qApp->activeModalWidget()))modal->reject();}
  });
  picker.start(10);QTest::mouseClick(dialog.findChild<QPushButton*>("updateChooseDirectory"),Qt::LeftButton);picker.stop();QVERIFY(selected);QVERIFY(!timeout);
  QVERIFY(dialog.findChild<QLabel*>("updateDestination")->text().contains(QDir::toNativeSeparators(folder.path())));
  const auto screenshots=qEnvironmentVariable("ASTER_UPDATE_SCREENSHOT_DIR");
  const QString variant=transferFallback?"-archive-fallback":mirrored?"-mirror":"";
  const QString stem=screenshots+"/download-"+language+variant;
  if(!screenshots.isEmpty()){QDir().mkpath(screenshots);QVERIFY(dialog.grab().save(stem+"-ready.png"));}
  QSignalSpy done(downloader,&aster::updates::PackageDownload::finished);QTest::mouseClick(button,Qt::LeftButton);
  QTRY_COMPARE(done.size(),1);QVERIFY(done[0][0].toBool());QVERIFY(!downloader->completedDirectory().isEmpty());QCOMPARE(button->text(),aster::ui::text("update.open_directory"));
  const QString actual=mirrored&&!transferFallback?"Gitee":"GitHub";
  QCOMPARE(manager.requests.last().url().host(),actual=="Gitee"?QString("gitee.com"):QString("github.com"));
  if(transferFallback){
   QCOMPARE(manager.requests.size(),5);
   QCOMPARE(manager.requests[3].url().host(),QString("gitee.com"));
  }
  QCOMPARE(announcement->text(),aster::ui::text("update.announcement_source").arg(notesSite));
  QVERIFY(announcement->isVisible());
  QCOMPARE(dialog.findChild<QLabel*>("updateReleaseMeta")->text(),aster::ui::text("update.transfer_source").arg(actual));
  if(!screenshots.isEmpty())QVERIFY(dialog.grab().save(stem+"-complete.png"));
  manager.responses.append(ReplyData{200,"[]"});
  QTest::mouseClick(dialog.findChild<QPushButton*>("updateCheck"),Qt::LeftButton);
  QVERIFY(!announcement->isVisible());QVERIFY(!dialog.findChild<QLabel*>("updateReleaseMeta")->isVisible());
  QTRY_VERIFY(!dialog.findChild<UpdateChecker*>()->busy());QVERIFY(!announcement->isVisible());
  QTest::keyClick(&dialog,Qt::Key_Escape);QVERIFY(!dialog.isVisible());
 }
 void settingsContactOpensLocalAuthorPage(){
  for (const auto *language : {"zh", "en"}) {
   aster::ui::setLanguage(language); QTemporaryDir dir; aster::ui::AppSettings settings;
   settings.databasePath=dir.filePath("results.sqlite"); aster::ui::SettingsDialog dialog(settings);
   auto *button=dialog.findChild<QPushButton*>("settingsContact"); QVERIFY(button);
   button->click(); auto *help=dialog.findChild<aster::ui::HelpDialog*>("helpDialog"); QVERIFY(help);
   QCOMPARE(help->currentTopic().path,QString("AUTHORS.md")); QVERIFY(help->isVisible());
   QVERIFY(!QFile::exists(settings.databasePath)); help->close();
  }
  aster::ui::setLanguage("zh");
 }
 void settingsWiringAndCatalogs(){for(auto lang:{"zh","en"}){aster::ui::setLanguage(lang);QTemporaryDir dir;aster::ui::AppSettings settings;settings.databasePath=dir.filePath("results.sqlite");aster::ui::SettingsDialog dialog(settings);auto *button=dialog.findChild<QPushButton*>("settingsCheckUpdates");QVERIFY(button);
   bool opened=false,hasToken=false,controlsPresent=false,checkMatches=false,cancelMatches=false,releasesEnabled=false,stopped=false,timedOut=false;
   QElapsedTimer elapsed;elapsed.start();QTimer poll;
   connect(&poll,&QTimer::timeout,&dialog,[&]{
    auto *child=dialog.findChild<QDialog*>("updateDialog");
    if(child && child->isVisible()){
     opened=true;hasToken=child->findChild<QLineEdit*>("updateToken")!=nullptr;
     auto *client=child->findChild<UpdateChecker*>();
     auto *check=child->findChild<QPushButton*>("updateCheck");
     auto *cancel=child->findChild<QPushButton*>("updateCancel");
     auto *download=child->findChild<QPushButton*>("updateOpen");
     auto *releases=child->findChild<QPushButton*>("updateReleases");
     controlsPresent=client && check && cancel && download && releases;
     if(controlsPresent){
      checkMatches=check->isEnabled()==(!isAlphaVersion(QCoreApplication::applicationVersion()) && !client->busy());
      cancelMatches=cancel->isEnabled()==client->busy();releasesEnabled=releases->isEnabled();
     }
     // Never assert inside this callback: an early return would strand exec().
     child->reject();stopped=client && !client->busy();poll.stop();
    }else if(elapsed.elapsed()>5000){
     timedOut=true;
     if(auto *modal=qobject_cast<QDialog*>(qApp->activeModalWidget()))modal->reject();
    }
   });
   poll.start(10);button->click();poll.stop();
   QVERIFY(!timedOut);QVERIFY(opened);QVERIFY(!hasToken);QVERIFY(controlsPresent);
   QVERIFY(checkMatches);QVERIFY(cancelMatches);QVERIFY(releasesEnabled);QVERIFY(stopped);
   QVERIFY(!QFile::exists(settings.databasePath));
   QFile f(":/updates/github.json");QVERIFY(f.open(QIODevice::ReadOnly));
   auto bundled = UpdateConfig::fromJson(f.readAll());
   QVERIFY(bundled.valid());
   QCOMPARE(bundled.releasesUrl(), QUrl("https://github.com/ChenCLian/AstroBatch/releases"));
   QCOMPARE(bundled.apiUrl(1, true).path(), QString("/api/v5/repos/ChenCLian/AstroBatch/releases"));
   QCOMPARE(bundled.packageMetadata, QString("AstroBatch-update.json"));
   bundled.repository = QStringLiteral("unrelated");
   QVERIFY(!bundled.valid());
  }
  aster::ui::setLanguage("zh");
 }
};
QTEST_MAIN(UpdateTests)
#include "update_tests.moc"
