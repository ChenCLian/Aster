// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
// Standalone validation probe; compile with the workspace test target's Qt/link flags.
#include "core/workflow_executor.h"
#include "processing/scientific_registry.h"
#include "io/database/result_repository.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <iostream>
#include <thread>
#include <chrono>
using namespace aster;
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv); const int columns=4096, rows=2048;
    GridData grid; grid.columns=columns;grid.rows=rows;
    grid.xAxis={"Time","ka"};grid.yAxis={"Period","ka"};grid.valueAxis={"Power","u"};
    for(int i=0;i<columns;++i)grid.x<<i;
    for(int i=0;i<rows;++i)grid.y<<std::exp2(i*.005);
    grid.values.resize(columns*rows);
    for(qsizetype i=0;i<grid.values.size();++i)grid.values[i]=std::sin(i*.01)+1;
    WorkflowModel m(makeScientificRegistry());auto root=m.addRoot("sine_source",{0,0}).nodeId;
    auto node=m.addChild(root,ChildSlot::Down,"cwt").nodeId;
    auto execution=WorkflowExecutor().run({m.freeze().snapshot,root,{"sampling benchmark",{},{}}});
    execution.nodes[node].outputs={{"power",makeData(std::move(grid))}};
    execution.nodes[node].state=NodeState::Succeeded;
    QTemporaryDir dir;auto path=dir.filePath("bench.sqlite");ResultRepository repo;
    if(!repo.open(path).isEmpty())return 1;
    auto receipt=repo.save(execution);if(!receipt.saved())return 2;
    OutputReference ref{path,receipt.recordId,execution.runId,node,"power","Grid","cwt"};
    execution.nodes[node].outputs.clear();
    PlotRequest request;request.columns=1200;request.rows=600;request.includeFamily=false;
    for(int iteration=0;iteration<3;++iteration) {
        QElapsedTimer clock;clock.start();auto out=repo.loadPlotPreview(ref,request);
        if(out.state!=NodeState::Succeeded)return 3;
        auto stats=out.report["plot_samples"].toObject()["power"].toObject();
        stats["mode"]=request.window?"viewport":"initial";stats["elapsed_ms"]=double(clock.elapsed());
        stats["bytes_read"]=out.report["bytes_read"];
        std::cout<<QJsonDocument(stats).toJson(QJsonDocument::Compact).constData()<<std::endl;
        request.window=true;request.xLower=200;request.xUpper=220;request.yLower=2;request.yUpper=4;
    }
    request.window=false;CancellationToken cancel;QElapsedTimer clock;clock.start();
    std::thread stop([cancel]{std::this_thread::sleep_for(std::chrono::milliseconds(20));cancel.cancel();});
    auto out=repo.loadPlotPreview(ref,request,cancel);stop.join();
    std::cout<<"cancelled="<<(out.state==NodeState::Cancelled)<<" elapsed_ms="<<clock.elapsed()<<std::endl;
    return out.state==NodeState::Cancelled?0:4;
}
