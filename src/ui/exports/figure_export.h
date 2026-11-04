#ifndef ASTER_FIGURE_EXPORT_H
#define ASTER_FIGURE_EXPORT_H
#include "ui/results/plot_style.h"
#include "io/database/plot_request.h"
#include "io/database/output_reference.h"
#include <QSize>
namespace aster::ui
{
struct FigureCell
{
    QVector<OutputReference> references;
    PlotStyle style;
    PlotRequest request;
};
struct FigureOptions
{
    enum class Format
    {
        Png,
        Pdf
    };
    QString path;
    Format format = Format::Png;
    QSize pixels{2400, 1600};
    int dpi = 200;
    bool whitePaper = true;
};
QString validateFigureOptions(const FigureOptions &options);
QString renderFigure(const QVector<FigureCell> &cells, const QVector<NodeResult> &data,
                     const FigureOptions &options);
} // namespace aster::ui
#endif
