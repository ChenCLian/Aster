// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
// Run one mode per process; Qt offscreen input is not native Windows input.
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QScrollArea>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
#include "processing/scientific_registry.h"
#include "ui/common/presentation.h"
#include "ui/panels/source_panel.h"
#include "ui/panels/file_options_editor.h"
#include "ui/results/plot_view.h"
#include "io/database/data_codec.h"
using namespace aster;
using namespace aster::ui;
double ms(QElapsedTimer& t) { return t.nsecsElapsed()/1e6; }
qint64 currentRss() {
 QFile f("/proc/self/status"); if(!f.open(QIODevice::ReadOnly))return -1;
 for(const auto &line:f.readAll().split('\n'))if(line.startsWith("VmRSS:"))return line.simplified().split(' ').value(1).toLongLong();
 return -1;
}
qint64 peakRss() {
#ifdef Q_OS_UNIX
 rusage r{}; getrusage(RUSAGE_SELF,&r);
#ifdef Q_OS_MACOS
 return r.ru_maxrss/1024;
#else
 return r.ru_maxrss;
#endif
#else
 return -1;
#endif
}
QJsonObject stats(QVector<double> v) {
 std::sort(v.begin(),v.end());if(v.isEmpty())return {};
 return {{"samples",v.size()},{"median_ms",v[v.size()/2]},{"p95_ms",v[std::max(0,int(std::ceil(v.size()*.95))-1)]},{"max_ms",v.back()}};
}
bool spinUntil(const std::function<bool()>& done,int timeout=60000) {
 QElapsedTimer t;t.start();while(!done() && t.elapsed()<timeout){QCoreApplication::processEvents(QEventLoop::AllEvents,5);QTest::qWait(1);}return done();
}
DataRef series(int n) {SeriesData s;for(int i=0;i<n;i++){s.x<<i;s.y<<std::sin(.12*i)+.2*std::cos(.35*i);}return makeData(s);}
int main(int argc,char**argv){
 QApplication app(argc,argv);setLanguage("en");applyTheme(app);
 const auto args=app.arguments();const auto mode=args.value(1,"numeric");const int count=args.value(2,"1000").toInt();
 QJsonObject out{{"mode",mode},{"count",count},{"qt",QT_VERSION_STR},{"platform",QGuiApplication::platformName()}};
 if(mode=="import" || mode=="remove") {
  if(count<1 || count>10000)return 2;
  QTemporaryDir dir;QStringList files;
  for(int i=0;i<count;i++){const auto path=dir.filePath(QString::number(i)+".txt");QFile f(path);if(!f.open(QIODevice::WriteOnly))return 3;f.write("x y\n0 1\n1 2\n2 4\n3 3\n");files<<path;}
  WorkflowDocument doc(makeScientificRegistry());SourcePanel panel(&doc);panel.resize(300,800);panel.show();
  QElapsedTimer clock,heartbeat;QVector<double> lateness;QTimer timer;timer.setInterval(10);
  heartbeat.start();QObject::connect(&timer,&QTimer::timeout,[&]{lateness<<std::max(0.,ms(heartbeat)-10);heartbeat.restart();});timer.start();
  clock.start();panel.addFiles(files);out["import_call_ms"]=ms(clock);
  auto* editor=panel.findChild<FileOptionsEditor*>();if(!spinUntil([&]{return editor->ready();}))return 4;
  auto* list=panel.findChild<QListWidget*>("sourceFiles");list->setFocus();
  clock.restart();QTest::keySequence(list,QKeySequence::SelectAll);out["select_all_event_ms"]=ms(clock);
  if(list->selectedItems().size()!=count)return 5;
  if(mode=="remove") {
   clock.restart();QTest::mouseClick(panel.findChild<QPushButton*>("removeSourceInput"),Qt::LeftButton);
   out["remove_event_ms"]=ms(clock);
   if(!doc.files(panel.root()).isEmpty())return 15;
   out["removed"]=count;
  } else {
   auto *toggle = panel.findChild<QToolButton *>("sourceOptionsToggle");
   if (!toggle->isChecked()) QTest::mouseClick(toggle, Qt::LeftButton);
   auto *unit = editor->findChild<QLineEdit *>("fileXUnit");
   if (auto *scroll = editor->findChild<QScrollArea *>()) scroll->ensureWidgetVisible(unit);
   if (!unit->isVisibleTo(&panel)) return 5;
   QTest::mouseClick(unit, Qt::LeftButton);
   QTest::keySequence(unit, QKeySequence::SelectAll);
   QSignalSpy done(&panel, &SourcePanel::batchOptionsFinished);
   clock.restart();
   QTest::keyClicks(unit, "kyr");
   out["type_unit_events_ms"] = ms(clock);
   QTest::keyClick(unit, Qt::Key_Tab);
   if (!spinUntil([&] {
       if (!panel.readyToRun()) return false;
       return count == 1 ? doc.fileOptions(panel.root(), files.first())["x_unit"] == "kyr"
                         : !done.isEmpty();
   })) return 6;
   out["edit_through_commit_ms"] = ms(clock);
   const int applied = count == 1 ? 1 : done[0][0].toStringList().size();
   const int skipped = count == 1 ? 0 : done[0][1].toStringList().size();
   out["applied"] = applied;
   out["skipped"] = skipped;
   for (const auto &source : doc.sources().value(panel.root()))
       if (source.provenance["file_options"].toObject()["x_unit"] != "kyr") return 7;
   if (applied != count || skipped) return 7;
  }
  spinUntil([&]{return lateness.size()>=100;},3000);timer.stop();out["event_loop_lateness"]=stats(lateness);
 } else if(mode=="numeric") {
  auto registry=makeScientificRegistry();QJsonArray runs;
  for(int n:{1001,10001,100001})for(const auto &settings:QStringList{"fixed","legacy_overlap","target_windows"}) {
   const bool preset=settings!="fixed";
   QJsonObject p{{"evo_window",200},{"evo_step",64},{"evo_nfft",800},{"evo_trend","linear"},{"evo_taper","rectangular"},{"evo_normalize",true}};
   if(preset){p["evo_window_mode"]="span_percent";p["evo_window_percent"]=20;p["evo_step_mode"]="overlap";p["evo_overlap_percent"]=99.2;p["evo_fft_mode"]="multiple";p["evo_padding"]=4;}
   if(settings=="target_windows"){p["evo_step_mode"]="target_windows";p["evo_target_windows"]=500;}
   QString error;auto task=registry.create("evolution_fft",p,&error);if(!task)return 9;
   const auto input=series(n);QVector<double> times;NodeResult result;
   for(int repeat=0;repeat<11;repeat++){QElapsedTimer t;t.start();result=task->execute({input},{});if(repeat)times<<ms(t);}
   const bool expectedSuccess=!preset || n==1001 || (settings=="target_windows" && n==10001);
   if((result.state==NodeState::Succeeded)!=expectedSuccess ||
       (!expectedSuccess && result.diagnostic().code!="EVOLUTION_BUDGET"))return 16;
   runs.append(QJsonObject{{"samples",n},{"preset",preset},{"settings",settings},{"timing",stats(times)},{"succeeded",result.state==NodeState::Succeeded},{"diagnostic",result.diagnostic().code},{"report",result.report}});
  }out["runs"]=runs;
 } else if(mode=="cancel") {
  auto registry=makeScientificRegistry();QString error;auto task=registry.create("evolution_fft",{{"evo_window",256},{"evo_step",64},{"evo_nfft",1024},{"frequency_max",.02}},&error);
  if(!task)return 10;
  const auto input=series(400001);ExecutionContext context;QElapsedTimer t;t.start();double requested=0;
  std::thread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(5));requested=ms(t);context.cancellation.cancel();});
  const auto result=task->execute({input},context);const double stopped=ms(t);cancel.join();
  out["cancelled"]=result.state==NodeState::Cancelled;out["request_ms"]=requested;out["returned_ms"]=stopped;out["cancel_response_ms"]=stopped-requested;
  if(result.state!=NodeState::Cancelled)return 11;
 } else if(mode=="plot") {
  GridData g;g.rows=256;g.columns=1024;g.xAxis={"Time","ka"};g.yAxis={"Frequency","1/ka"};g.valueAxis={"Relative power","1"};
  for(int i=0;i<g.columns;i++)g.x<<i;
  for(int i=0;i<g.rows;i++)g.y<<.001*(i+1);
  for(int r=0;r<g.rows;r++)for(int c=0;c<g.columns;c++)g.values<<(.1+std::pow(std::sin(.01*c+.03*r),2));
  auto data=makeData(g);const auto before=QCryptographicHash::hash(encodeData(data).bytes,QCryptographicHash::Sha256);
  auto result=NodeResult::success({{"power_map",data}});PlotView view;view.resize(900,650);view.show();view.showOutput(result,"power_map");
  QCoreApplication::processEvents();QVector<double> times;QJsonArray memory;
  for(int i=0;i<110;i++){auto style=view.style();style.colorAmplitude=i%2;style.colorInterpolate=(i/2)%2;
   QElapsedTimer t;t.start();const auto error=view.applyStyle(style);QCoreApplication::processEvents();if(!error.isEmpty())return 12;
   if(i>=10)times<<ms(t);
   if(i%10==9)memory.append(double(currentRss()));}
  if(before!=QCryptographicHash::hash(encodeData(data).bytes,QCryptographicHash::Sha256))return 13;
  out["style_and_render_timing"]=stats(times);out["rss_every_10_toggles_kib"]=memory;out["raw_data_unchanged"]=true;
 } else return 14;
 out["peak_rss_kib"]=double(peakRss());out["rss_kib"]=double(currentRss());
 fprintf(stdout,"%s\n",QJsonDocument(out).toJson(QJsonDocument::Compact).constData());return 0;
}
