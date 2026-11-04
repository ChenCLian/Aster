#include "ui/common/presentation.h"
#include "ui/exports/figure_export.h"
#include "ui/results/plot_view.h"
#include "third_party/qcustomplot/qcustomplot.h"
#include <QSaveFile>
#include <QPdfWriter>
#include <QPageSize>
#include <QImageWriter>
#include <QApplication>
#include "io/exports/export_path.h"
namespace aster::ui
{
namespace
{
constexpr double layoutDpi = 96.;
QFont layoutFont(QFont font)
{
    // QCustomPlot measures with screen QFontMetrics. Pixel fonts make those
    // metrics agree with both PNG and PDF devices, independently of output DPI.
    if (font.pointSizeF() > 0)
        font.setPixelSize(qRound(font.pointSizeF() * layoutDpi / 72.));
    return font;
}
void freezeFonts(QCustomPlot *plot)
{
    for (auto *axis : plot->findChildren<QCPAxis *>())
    {
        axis->setTickLabelFont(layoutFont(axis->tickLabelFont()));
        axis->setLabelFont(layoutFont(axis->labelFont()));
    }
    for (int i = 0; i < plot->graphCount(); ++i)
        plot->graph(i)->setAdaptiveSampling(false);
    plot->setPlottingHint(QCP::phFastPolylines, false);
    plot->legend->setFont(layoutFont(plot->legend->font()));
    for (int i = 0; i < plot->legend->itemCount(); ++i)
        plot->legend->item(i)->setFont(layoutFont(plot->legend->item(i)->font()));
    for (auto *title : plot->findChildren<QCPTextElement *>())
        title->setFont(layoutFont(title->font()));
}
} // namespace

QString validateFigureOptions(const FigureOptions &o)
{
    if (o.path.trimmed().isEmpty())
        return text("error.choose_an_export_file");
    if (o.pixels.width() < 320 || o.pixels.height() < 240 ||
        o.pixels.width() > PlotLimits::maxDimension ||
        o.pixels.height() > PlotLimits::maxDimension ||
        qint64(o.pixels.width()) * o.pixels.height() > PlotLimits::maxExportPixels)
        return text("error.export_dimensions_exceed_the_allowed_range_320_x_240_to_8192_x_8192_at");
    if (o.dpi < 72 || o.dpi > 600)
        return text("error.export_dpi_must_be_between_72_and_600");
    if (o.format != FigureOptions::Format::Png && o.format != FigureOptions::Format::Pdf)
        return text("error.unknown_figure_format");
    return {};
}
QString renderFigure(const QVector<FigureCell> &cells, const QVector<NodeResult> &data,
                     const FigureOptions &o)
{
    const auto error = validateFigureOptions(o);
    if (!error.isEmpty())
        return error;
    if (QThread::currentThread() != qApp->thread())
        return text("error.figure_painting_must_run_on_the_gui_thread");
    if (cells.isEmpty() || cells.size() > 4 || data.size() != cells.size())
        return text("error.invalid_figure_layout");
    for (const auto &cell : cells)
    {
        if (cell.references.isEmpty() || cell.references[0].kind == "Table")
            return text("error.tables_must_be_exported_as_csv");
        for (const auto &ref : cell.references)
            if (conflictsWithDatabase(o.path, {ref.databasePath}))
                return text("error.cannot_overwrite_a_source_database");
    }
    QSaveFile output(o.path);
    if (!output.open(QIODevice::WriteOnly))
        return output.errorString();
    const int columns = cells.size() > 1 ? 2 : 1, rows = (cells.size() + columns - 1) / columns;
    auto paint = [&](QCPPainter &p) -> QString
    {
        p.fillRect(QRect(QPoint(), o.pixels), o.whitePaper ? Qt::white : cells[0].style.background);
        for (int i = 0; i < cells.size(); ++i)
        {
            const int left = i % columns * o.pixels.width() / columns,
                      right = (i % columns + 1) * o.pixels.width() / columns;
            const int top = i / columns * o.pixels.height() / rows,
                      bottom = (i / columns + 1) * o.pixels.height() / rows;
            const double scale = o.dpi / layoutDpi;
            const int logicalWidth = qRound((right - left) / scale),
                      logicalHeight = qRound((bottom - top) / scale);
            if (logicalWidth < 220 || logicalHeight < 160)
                return text("error.increase_export_size_or_reduce_dpi_for_this_subplot_layout");
            PlotView view;
            view.resize(logicalWidth, logicalHeight);
            const auto &cell = cells[i];
            auto style = cell.style;
            if (style.title.isEmpty())
                style.title = QString("ID %1 · %2")
                                  .arg(cell.references[0].recordId)
                                  .arg(cell.references[0].port);
            // Freeze the captured appearance. Export does not mutate the on-screen view.
            style.followTheme = false;
            if (o.whitePaper)
            {
                style.background = Qt::white;
                style.backgroundImage.clear();
                for (auto *axis : {&style.x, &style.y})
                {
                    axis->axisColor = QColor(95, 110, 130);
                    axis->gridColor = QColor(220, 227, 236);
                    axis->subGridColor = QColor(236, 240, 245);
                }
            }
            view.showOutput(data[i], cell.references[0].port);
            auto failure = view.applyStyle(style);
            if (!failure.isEmpty())
                return failure;
            if (!view.hasPlot())
                return text("error.output_cannot_be_rendered_as_a_figure");
            if (!view.contourError().isEmpty())
                return view.contourError();
            freezeFonts(view.plotWidget());
            p.save();
            p.translate(left, top);
            p.setClipRect(QRect(0, 0, right - left, bottom - top));
            p.scale(scale, scale);
            view.plotWidget()->toPainter(&p, logicalWidth, logicalHeight);
            p.restore();
        }
        return {};
    };
    QString failure;
    if (o.format == FigureOptions::Format::Png)
    {
        QImage image(o.pixels, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull())
            return text("error.cannot_allocate_export_image");
        image.setDotsPerMeterX(qRound(o.dpi / 0.0254));
        image.setDotsPerMeterY(qRound(o.dpi / 0.0254));
        QCPPainter painter(&image);
        failure = paint(painter);
        painter.end();
        if (failure.isEmpty())
        {
            QImageWriter writer(&output, "png");
            if (!writer.write(image))
                failure = writer.errorString();
        }
    }
    else
    {
        QPdfWriter writer(&output);
        writer.setCreator("Aster");
        writer.setResolution(o.dpi);
        writer.setPageSize(
            QPageSize(QSizeF(o.pixels.width() * 25.4 / o.dpi, o.pixels.height() * 25.4 / o.dpi),
                      QPageSize::Millimeter));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QCPPainter painter(&writer);
        painter.setMode(QCPPainter::pmVectorized, true);
        painter.setMode(QCPPainter::pmNoCaching, true);
        if (!painter.isActive())
            return text("error.cannot_initialize_pdf_painter");
        failure = paint(painter);
        painter.end();
    }
    if (!failure.isEmpty())
    {
        output.cancelWriting();
        return failure;
    }
    return output.commit() ? QString() : output.errorString();
}
} // namespace aster::ui
