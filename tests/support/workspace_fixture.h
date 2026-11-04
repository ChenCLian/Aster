#ifndef ASTER_TESTS_WORKSPACE_FIXTURE_H
#define ASTER_TESTS_WORKSPACE_FIXTURE_H
// Synthetic data belongs to tests, not to the desktop application.
#include "ui/app/main_window.h"
#include "ui/workflow/workflow_canvas.h"
#include "ui/workflow/workflow_document.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>
namespace aster::testsupport
{
inline std::unique_ptr<QTemporaryDir> installWorkspaceFixture(ui::MainWindow &window)
{
    auto directory = std::make_unique<QTemporaryDir>();
    if (!directory->isValid()) return {};
    QStringList paths;
    for (int sample = 0; sample < 3; ++sample)
    {
        const auto path = directory->filePath(QString("Sample_%1.csv").arg(QChar('A' + sample)));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
        QTextStream out(&file);
        out.setRealNumberPrecision(17);
        out << "Time,Signal\n";
        for (int i = 0; i < 512; ++i)
        {
            const double t = i;
            const double y = .003 * t + std::sin(6.28318530718 * t / (19 + 2 * sample)) +
                             .45 * std::cos(6.28318530718 * t / 43) +
                             .6 * std::exp(-std::pow((t - 300) / 90, 2)) *
                                 std::sin(6.28318530718 * (t / 9 + .00004 * t * t));
            out << t << ',' << y << '\n';
        }
        paths.push_back(path);
    }
    auto *document = window.document();
    WorkflowModel model(document->model().registry());
    const auto root = model.addRoot("file_source", {0, 0}).nodeId;
    const auto prep = model.addChild(root, ChildSlot::Down, "preprocess").nodeId;
    const auto trend = model.addChild(prep, ChildSlot::Down, "detrend_lowess",
                                     {{"fraction", .25}}).nodeId;
    const auto spectrum = model.addChild(trend, ChildSlot::Left, "mtm", {{"nw", 3}}).nodeId;
    model.addChild(trend, ChildSlot::Right, "cwt");
    model.addChild(trend, ChildSlot::Down, "series_summary");
    if (!document->replaceDefinition(model.definition()).ok()) return {};
    document->setFiles(root, paths);
    window.canvas()->nodeSelected(spectrum);
    window.canvas()->fitWorkflow();
    window.setPage(0);
    return directory;
}
} // namespace aster::testsupport
#endif
