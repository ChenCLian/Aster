// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDebug>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QTableWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
#include "application/batch_runner.h"
#include "io/database/data_codec.h"
#include "processing/cross_spectrum_task.h"
#include "processing/scientific_registry.h"
#include "third_party/qcustomplot/qcustomplot.h"
#include "ui/common/presentation.h"
#include "ui/panels/pairing_panel.h"
#include "ui/panels/joint_input_dialog.h"
#include "ui/results/plot_view.h"
#include "ui/services/workflow_preview_controller.h"
#include "ui/workflow/workflow_document.h"
#include "ui/app/main_window.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
using namespace aster;
using namespace aster::ui;
DataRef series(int n, int k = 0)
{
    SeriesData a;
    a.xAxis = {"Time", "ka"};
    a.yAxis = {"Signal", "u"};
    a.x.reserve(n);
    a.y.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        a.x << i * .25;
        a.y << std::sin(.13 * i + k * .1) + .2 * std::cos(.071 * i + k * .2);
    }
    return makeData(a);
}
qint64 rss()
{
#ifdef Q_OS_UNIX
    rusage r{};
    getrusage(RUSAGE_SELF, &r);
#ifdef Q_OS_MACOS
    return r.ru_maxrss / 1024;
#else
    return r.ru_maxrss;
#endif
#else
    return -1; // Peak RSS requires an external monitor on this platform.
#endif
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setLanguage("en");
    applyTheme(app);
    const auto args = app.arguments();
    const QString mode = args.value(1, "numeric");
    const int count = args.value(2, "1000000").toInt();
    if (count < 1 ||
        ((mode == "numeric" || mode == "plot" || mode == "cancel") && count > 1000000) ||
        ((mode == "dialog" || mode == "linked") && count > 9999) ||
        (mode == "gui" && count > 1000) || (mode == "batch" && count > 64))
        return 8;
    QJsonObject out{{"mode", mode}, {"count", count}};
    QElapsedTimer whole;
    whole.start();
    if (mode == "numeric" || mode == "plot" || mode == "cancel")
    {
        const int seg = mode == "plot" ? 131072 : 4096, fft = mode == "plot" ? 262144 : 8192;
        auto a = series(count), b = series(count, 1);
        QJsonObject p{{"psd_window", seg}, {"psd_overlap", seg / 2}, {"psd_nfft", fft}};
        if (mode == "cancel")
            p["psd_overlap"] = 3584;
        QElapsedTimer clock;
        clock.start();
        ExecutionContext context;
        std::thread stopper;
        std::atomic<qint64> requestedAt{-1};
        if (mode == "cancel")
            stopper = std::thread([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                requestedAt = clock.elapsed();
                context.cancellation.cancel();
            });
        auto result = CrossSpectrumTask(p).execute({a, b}, context);
        if (stopper.joinable())
            stopper.join();
        out["compute_ms"] = double(clock.elapsed());
        if (requestedAt >= 0)
        {
            out["cancel_requested_ms"] = double(requestedAt.load());
            out["cancel_response_ms"] = double(clock.elapsed() - requestedAt.load());
        }
        out["state"] = int(result.state);
        out["error"] = result.error;
        out["rss_after_compute_kib"] = double(rss());
        if ((mode == "cancel" && result.state != NodeState::Cancelled) ||
            (mode != "cancel" && result.state != NodeState::Succeeded))
        {
            qInfo().noquote() << QJsonDocument(out).toJson(QJsonDocument::Compact);
            return 1;
        }
        if (mode == "plot")
        {
            PlotView view;
            view.resize(1000, 650);
            view.show();
            app.processEvents();
            clock.restart();
            view.showOutput(result, "cross_spectrum");
            app.processEvents();
            out["first_plot_ms"] = double(clock.elapsed());
            auto *combo = view.findChild<QComboBox *>("crossMetric");
            QJsonArray switches;
            for (int repeat = 0; repeat < 2; ++repeat)
                for (int i = 0; i < combo->count(); ++i)
                {
                    clock.restart();
                    combo->setCurrentIndex(i);
                    app.processEvents();
                    switches << QJsonObject{{"metric", combo->currentData().toString()},
                                            {"ms", double(clock.elapsed())}};
                }
            out["metric_switches"] = switches;
            combo->setCurrentIndex(0);
            app.processEvents();
            clock.restart();
            for (int i = 0; i < 20; ++i)
            {
                view.plotWidget()->xAxis->scaleRange(.98);
                view.plotWidget()->replot();
            }
            out["twenty_zoom_ms"] = double(clock.elapsed());
            QTemporaryDir tmp;
            clock.restart();
            auto error = view.exportCsv(tmp.filePath("cross.csv"));
            out["csv_ms"] = double(clock.elapsed());
            out["csv_error"] = error;
            out["csv_bytes"] = double(QFileInfo(tmp.filePath("cross.csv")).size());
            clock.restart();
            out["png_ok"] = view.exportPng(tmp.filePath("cross.png"));
            out["png_ms"] = double(clock.elapsed());
            if (!error.isEmpty() || !out["png_ok"].toBool()) return 9;
            QJsonArray memory;
            for (int cycle = 0; cycle < 10; ++cycle)
            {
                for (int metric = 0; metric < combo->count(); ++metric)
                {
                    combo->setCurrentIndex(metric);
                    app.processEvents();
                }
                memory << double(rss());
            }
            out["repeated_metric_cycles_peak_rss_kib"] = memory;
        }
    }
    else if (mode == "dialog" || mode == "batch")
    {
        WorkflowDocument doc(makeScientificRegistry());
        auto root = doc.addRoot("input_series").nodeId;
        auto joint = doc.addChild(root, ChildSlot::Down, "cross_spectrum",
                                  {{"psd_window", 256}, {"psd_overlap", 128}})
                         .nodeId;
        QVector<SourceItem> sources;
        auto small = series(512);
        for (int i = 0; i <= count; ++i)
            sources << SourceItem{
                QString("Data %1").arg(i), mode == "dialog" ? small : series(131072, i), {}};
        if (!doc.setSources(root, sources).isEmpty())
            return 2;
        auto ds = doc.datasets(root);
        QVector<BatchPair> pairs;
        for (int i = 0; i < count; ++i)
            pairs << BatchPair{QUuid::createUuid(),
                               QString("Pair %1").arg(i),
                               {ds[i].id, "series"},
                               {ds.back().id, "series"}};
        QElapsedTimer clock;
        clock.start();
        const auto error = doc.setPairs(joint, pairs);
        out["set_pairs_ms"] = double(clock.elapsed());
        if (!error.isEmpty())
        {
            qWarning() << error;
            return 3;
        }
        if (mode == "dialog")
        {
            PairingPanel panel(&doc);
            panel.selectNode(joint);
            panel.show();
            app.processEvents();
            clock.restart();
            panel.findChild<QPushButton *>("managePairs")->click();
            auto* dialog = panel.findChild<PairingDialog*>();
            if (!dialog || !dialog->isVisible()) return 10;
            out["dialog_construct_ms"] = double(clock.elapsed());
            app.processEvents();
            out["dialog_first_paint_ms"] = double(clock.elapsed());
            out["selectors"] = dialog->findChildren<QComboBox*>().size();
            out["rss_dialog_kib"] = double(rss());
            auto* table = dialog->findChild<QTableWidget*>("pairTable");
            if (!table || table->rowCount() != count) return 10;
            table->setCurrentCell(count - 1, 2);
            clock.restart();
            dialog->findChild<QPushButton*>("choosePairB")->click();
            auto* picker = dialog->findChild<JointInputDialog*>();
            if (!picker || !picker->isVisible()) return 10;
            app.processEvents();
            out["member_chooser_ms"] = double(clock.elapsed());
            auto* candidates = picker->findChild<QTableView*>("jointMemberTable");
            if (!candidates || candidates->model()->rowCount() != count + 1) return 10;
            candidates->setCurrentIndex(candidates->model()->index(count - 1, 0));
            clock.restart();
            picker->findChild<QPushButton*>("jointInputConfirm")->click();
            app.processEvents();
            out["change_member_ms"] = double(clock.elapsed());
            dialog->reject();
            app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
            // Closing the draft must not change the document or its identities.
            const auto original = doc.grouping().value(joint);
            if (original.size() != pairs.size() || !(original.back().b == pairs.back().b)) return 10;
            WorkflowPreviewController preview(&doc);
            clock.restart();
            preview.selectNode(joint);
            preview.selectPair(pairs.front().id);
            preview.setActive(true);
            out["preview_prepare_ms"] = double(clock.elapsed());
            QElapsedTimer wait;
            wait.start();
            while (preview.state() != WorkflowPreviewController::State::Ready &&
                   wait.elapsed() < 10000)
            {
                app.processEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            out["preview_ready_ms"] = double(wait.elapsed());
            out["preview_state"] = int(preview.state());
            if (preview.state() != WorkflowPreviewController::State::Ready) return 11;
        }
        else
        {
            QTemporaryDir tmp;
            QJsonArray runs;
            for (int workers : {1, 4})
            {
                auto path = tmp.filePath(QString::number(workers) + ".sqlite");
                clock.restart();
                auto batch = BatchRunner().run(doc.plan(), path, workers);
                if (!batch.error.isEmpty() || batch.items.size() != 2 * count + 1)
                    return 4;
                QJsonObject run{{"workers", workers},
                                {"elapsed_ms", double(clock.elapsed())},
                                {"error", batch.error},
                                {"records", batch.items.size()},
                                {"database_bytes", double(QFileInfo(path).size())}};
                ResultRepository repo;
                if (!repo.open(path, false).isEmpty())
                    return 4;
                QMap<QString, QString> hashes;
                for (const auto &item : batch.items)
                {
                    if (!item.persistence.saved() || item.computation != RunState::Succeeded)
                        return 5;
                    auto r = repo.load(item.persistence.recordId);
                    if (!r.ok())
                        return 6;
                    if (r.run.nodes.contains(joint))
                        hashes[item.sourceLabel] = QString(
                            QCryptographicHash::hash(
                                encodeData(r.run.nodes[joint].outputs["cross_spectrum"]).bytes,
                                QCryptographicHash::Sha256)
                                .toHex());
                }
                if (hashes.size() != count) return 6;
                QJsonObject h;
                for (auto it = hashes.begin(); it != hashes.end(); ++it)
                    h[it.key()] = it.value();
                run["pair_hashes"] = h;
                runs << run;
            }
            out["runs"] = runs;
            if (runs.size() != 2 || runs[0].toObject()["pair_hashes"] != runs[1].toObject()["pair_hashes"])
                return 12;
        }
    }
    else if (mode == "gui")
    {
        QTemporaryDir tmp;
        AppSettings settings;
        settings.databasePath = tmp.filePath("gui.sqlite");
        settings.language = "en"; settings.concurrency = 4;
        MainWindow window(settings, false);
        window.resize(1450, 900); window.show();
        auto* doc = window.document();
        const auto rootA = doc->addRoot("input_series").nodeId;
        const auto rootB = doc->addRoot("input_series").nodeId;
        const auto upstream = doc->addChild(rootB, ChildSlot::Down, "affine", {{"factor",2}}).nodeId;
        const auto joint = doc->addChild(rootA, ChildSlot::Down, "cross_spectrum").nodeId;
        QVector<SourceItem> sources;
        for (int i = 0; i < count; ++i) sources.push_back({QString::number(i), series(4096,i), {}});
        if (!doc->setSources(rootA, sources).isEmpty() ||
            !doc->setSources(rootB, {{"reference", series(4096,1), {}}}).isEmpty()) return 15;
        const auto ref = doc->datasets(rootB).front().id;
        QVector<BatchPair> pairs;
        for (const auto& d : doc->datasets(rootA))
            pairs.push_back({QUuid::createUuid(),d.source.label,{d.id,"series"},{ref,"series",upstream}});
        if (!doc->setPairs(joint,pairs).isEmpty()) return 15;
        app.processEvents();
        out["setup_ms"] = double(whole.elapsed());
        QElapsedTimer clock; clock.start();
        qint64 last = 0, maximumGap = 0; int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        QObject::connect(&heartbeat,&QTimer::timeout,&window,[&] {
            const auto now=clock.elapsed(); maximumGap=std::max(maximumGap,now-last);last=now;++ticks;
        });
        std::shared_ptr<const BatchResult> result;
        QObject::connect(window.runner(),&RunController::batchReady,&window,
                         [&](std::shared_ptr<const BatchResult> batch) {result=std::move(batch);});
        heartbeat.start(); window.startRun();
        if (!window.runner()->busy()) return 16;
        while (window.runner()->busy() && clock.elapsed()<120000) {
            app.processEvents(QEventLoop::AllEvents,20);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        heartbeat.stop();
        maximumGap=std::max(maximumGap,clock.elapsed()-last);
        out["run_ms"]=double(clock.elapsed()); out["heartbeat_ticks"]=ticks;
        out["maximum_event_loop_gap_ms"]=double(maximumGap);
        if (!result || window.runner()->busy() || !result->error.isEmpty() || result->items.size()!=2*count+1)
            return 17;
        for (const auto& item : result->items)
            if (!item.persistence.saved() || item.computation!=RunState::Succeeded) return 17;
        out["records"]=result->items.size(); out["database_bytes"]=double(QFileInfo(settings.databasePath).size());
    }
    else if (mode == "linked")
    {
        WorkflowDocument doc(makeScientificRegistry());
        const auto rootA = doc.addRoot("input_series").nodeId;
        const auto rootB = doc.addRoot("input_series").nodeId;
        const auto upstream = doc.addChild(rootB, ChildSlot::Down, "affine", {{"factor", 2}}).nodeId;
        const auto joint = doc.addChild(rootA, ChildSlot::Down, "cross_spectrum").nodeId;
        QVector<SourceItem> sources;
        const auto input = series(512);
        for (int i = 0; i < count; ++i) sources.push_back({QString::number(i), input, {}});
        if (!doc.setSources(rootA, sources).isEmpty() ||
            !doc.setSources(rootB, {{"reference", series(512, 1), {}}}).isEmpty()) return 13;
        const auto reference = doc.datasets(rootB).front().id;
        QVector<BatchPair> pairs;
        for (const auto& dataset : doc.datasets(rootA))
            pairs.push_back({QUuid::createUuid(), dataset.source.label,
                             {dataset.id, "series"}, {reference, "series", upstream}});
        if (!doc.setPairs(joint, pairs).isEmpty()) return 13;
        QElapsedTimer clock; clock.start();
        const auto plan = doc.plan();
        out["prepare_ms"] = double(clock.elapsed());
        if (!plan.validation.ok() || plan.batches.size() != 1) return 13;
        out["jobs"] = plan.batches.front()->jobs().size();
        QJsonArray runs;
        QMap<QUuid, QByteArray> expected;
        for (int workers : {1, 4, 8}) {
            QMap<QUuid, QByteArray> hashes;
            BatchObserver observer;
            observer.completed = [&](const BatchJob& job, const NodeResult& result) {
                if (job.node == joint && result.state == NodeState::Succeeded)
                    hashes[job.item] = QCryptographicHash::hash(
                        encodeData(result.outputs.value("cross_spectrum")).bytes, QCryptographicHash::Sha256);
            };
            clock.restart();
            const auto run = BatchWorkflowExecutor().run(plan.batches.front(), {workers}, observer);
            if (run.state != RunState::Succeeded || hashes.size() != count ||
                (workers != 1 && hashes != expected)) return 14;
            if (workers == 1) expected = hashes;
            runs.append(QJsonObject{{"workers", workers}, {"elapsed_ms", double(clock.elapsed())},
                                     {"pairs", hashes.size()},
                                     {"peak_cache_bytes", double(run.peakCachedPayloadBytes)}});
        }
        out["runs"] = runs;
        out["parallel_outputs_identical"] = true;
    }
    else
        return 7;
    out["peak_rss_kib"] = double(rss());
    out["total_ms"] = double(whole.elapsed());
    qInfo().noquote() << QJsonDocument(out).toJson(QJsonDocument::Compact);
    return 0;
}
