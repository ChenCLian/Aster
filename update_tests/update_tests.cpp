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
UpdateConfig config(){return {"ChenCLian","Aster","2026-03-10"};}
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
   QVERIFY(updates.findChild<QLabel*>("updateStatus")->text().isEmpty());
   auto *notes=updates.findChild<QPlainTextEdit*>("updateNotes");
   QVERIFY(notes->placeholderText().isEmpty());
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
  p=parseReleasePage(json({o}),c,false);QCOMPARE(p.releases[0].pageUrl,QUrl("https://github.com/ChenCLian/Aster/releases/tag/v1.0.1"));
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
  LoopbackManager m;m.port=server.serverPort();UpdateChecker c(config(),nullptr,&m);QSignalSpy done(&c,&UpdateChecker::finished);c.check("1.0.0");QVERIFY(done.wait());QCOMPARE(qvariant_cast<UpdateResult>(done[0][0]).state,UpdateResult::State::Available);QCOMPARE(m.requested,config().apiUrl(1));QVERIFY(received.contains("/repos/ChenCLian/Aster/releases?per_page=100&page=1"));QVERIFY(!received.toLower().contains("authorization:"));
 }
 void settingsWiringAndCatalogs(){for(auto lang:{"zh","en"}){aster::ui::setLanguage(lang);QTemporaryDir dir;aster::ui::AppSettings settings;settings.databasePath=dir.filePath("results.sqlite");aster::ui::SettingsDialog dialog(settings);auto *button=dialog.findChild<QPushButton*>("settingsCheckUpdates");QVERIFY(button);
   bool opened=false;QTimer::singleShot(10,&dialog,[&]{auto *child=dialog.findChild<QDialog*>("updateDialog");if(child){opened=true;auto *input=child->findChild<QLineEdit*>("updateToken");QVERIFY(!input);QVERIFY(child->findChild<QPushButton*>("updateCheck")->isEnabled());QVERIFY(!child->findChild<QPushButton*>("updateOpen")->isEnabled());QVERIFY(child->findChild<QPushButton*>("updateReleases")->isEnabled());child->reject();}});button->click();QVERIFY(opened);QVERIFY(!QFile::exists(settings.databasePath));
   QFile f(":/updates/github.json");QVERIFY(f.open(QIODevice::ReadOnly));QVERIFY(UpdateConfig::fromJson(f.readAll()).valid());
  }
  aster::ui::setLanguage("zh");
 }
};
QTEST_MAIN(UpdateTests)
#include "update_tests.moc"
