#include "ui/panels/file_preview_panel.h"
#include "ui/services/file_preview_loader.h"
#include "ui/common/presentation.h"
#include "ui/common/error_presenter.h"
#include "ui/settings/theme_manager.h"
#include "third_party/qcustomplot/qcustomplot.h"
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QShowEvent>
#include <QHideEvent>
#include <algorithm>
namespace aster::ui
{
namespace
{
QString axisLabel(const AxisInfo &axis)
{
    return axis.unit.isEmpty() ? axis.name : axis.name + " (" + axis.unit + ')';
}
} // namespace
FilePreviewPanel::FilePreviewPanel(QWidget *parent) : QFrame(parent)
{
    setObjectName("filePreviewPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setContentsMargins(8, 4, 8, 6);
    layout->setSpacing(2);
    auto *identity = new QFrame; identity->setObjectName("SourcePreviewIdentity");
    auto *header = new QVBoxLayout(identity); header->setContentsMargins(10, 9, 10, 9);
    header->setSpacing(4);
    m_title = new QLabel;
    m_title->setObjectName("PreviewMethod");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(m_title, 1);
    m_filename = new QLabel; m_filename->setTextFormat(Qt::PlainText);
    m_filename->setMinimumWidth(0); m_filename->setWordWrap(true);
    m_filename->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(m_filename);
    layout->addWidget(identity);
    m_body = new QWidget;
    m_body->setObjectName("filePreviewBody");
    auto *body = new QVBoxLayout(m_body);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(2);
    m_message = new QLabel;
    m_message->setObjectName("filePlotMessage");
    m_message->setWordWrap(true);
    m_message->setTextFormat(Qt::PlainText);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    body->addWidget(m_message, 1);
    m_plot = new QCustomPlot;
    m_plot->setObjectName("filePreviewPlot");
    m_plot->setMinimumSize(0, 0);
    m_plot->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->setNoAntialiasingOnDrag(true);
    m_plot->axisRect()->setMinimumMargins(QMargins(4, 4, 4, 4));
    m_plot->xAxis->ticker()->setTickCount(4);
    m_plot->yAxis->ticker()->setTickCount(4);
    m_curve = new QCPCurve(m_plot->xAxis, m_plot->yAxis);
    body->addWidget(m_plot, 1);
    auto *footer = new QHBoxLayout;
    m_summary = new QLabel;
    m_summary->setObjectName("Muted");
    m_summary->setWordWrap(true);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    footer->addWidget(m_summary, 1);
    auto *fitButton = new QToolButton;
    fitButton->setText(text("ui.fit"));
    fitButton->setObjectName("filePreviewFit");
    footer->addWidget(fitButton);
    body->addLayout(footer);
    layout->addWidget(m_body, 1);
    m_loader = new FilePreviewLoader(this);
    connect(m_loader, &FilePreviewLoader::changed, this, &FilePreviewPanel::updatePreview);
    connect(fitButton, &QToolButton::clicked, this, &FilePreviewPanel::fit);
    connect(m_plot, &QCustomPlot::mouseDoubleClick, this, [this] { fit(); });
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, &FilePreviewPanel::applyColors);
    updateTitle();
    applyColors();
    updatePreview();
}
void FilePreviewPanel::setInput(const QString &path, const QJsonObject &options, bool ready)
{
    if (m_path != path || m_options != options || m_ready != ready) m_executionSource.reset();
    m_path = path; m_options = options; m_ready = ready;
    updateTitle();
    m_loader->setInput(path, options, ready);
}
void FilePreviewPanel::setWorkflowSource(const NodeResult &source)
{
    // The completed path is authoritative: upper and lower plots must describe
    // the same read even if an external editor changed the disk file meanwhile.
    auto snapshot = std::make_shared<FilePreviewData>();
    if (const auto *data = dataAs<SeriesData>(source.outputs.value("series")))
    {
        snapshot->samples = *data;
        snapshot->rows = qsizetype(source.report["rows"].toDouble(data->x.size()));
        snapshot->skippedRows = qsizetype(source.report["skipped_rows"].toDouble());
    }
    else if (!source.error.isEmpty()) snapshot->failure = source;
    else return;
    m_executionSource = std::move(snapshot);
    updatePreview();
}
void FilePreviewPanel::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    m_loader->setActive(true);
}
void FilePreviewPanel::hideEvent(QHideEvent *event)
{
    m_executionSource.reset();
    m_loader->setActive(false);
    QFrame::hideEvent(event);
}
void FilePreviewPanel::updateTitle()
{
    const auto title = text("ui.preview_original");
    m_title->setText(title);
    m_title->setToolTip(m_path);
    m_filename->setText(QFileInfo(m_path).fileName());
    m_filename->setToolTip(m_path);
    m_filename->setVisible(!m_path.isEmpty());
    m_title->parentWidget()->setVisible(!m_path.isEmpty());
}
void FilePreviewPanel::updatePreview()
{
    m_curve->data()->clear();
    m_plot->xAxis->setLabel({});
    m_plot->yAxis->setLabel({});
    m_message->setToolTip({});
    m_summary->clear();
    m_summary->setToolTip({});
    const auto state = m_executionSource ? (m_executionSource->ok() ? FilePreviewLoader::State::Ready
                                                                     : FilePreviewLoader::State::Failed)
                                        : m_loader->state();
    m_plot->setVisible(state == FilePreviewLoader::State::Ready);
    m_message->setVisible(state != FilePreviewLoader::State::Ready);
    findChild<QToolButton *>("filePreviewFit")->setEnabled(state == FilePreviewLoader::State::Ready);
    if (state == FilePreviewLoader::State::Ready)
    {
        const auto result = m_executionSource ? m_executionSource : m_loader->result();
        m_curve->setData(result->samples.x, result->samples.y); // QCPCurve retains row order.
        m_plot->xAxis->setLabel(axisLabel(result->samples.xAxis));
        m_plot->yAxis->setLabel(axisLabel(result->samples.yAxis));
        auto summary = result->rows == result->samples.x.size()
            ? text("ui.file_plot_points").arg(result->rows)
            : text("ui.file_plot_reduced").arg(result->samples.x.size()).arg(result->rows);
        if (result->skippedRows) summary += text("ui.file_plot_skipped").arg(result->skippedRows);
        m_summary->setText(summary);
        m_summary->setToolTip(text("ui.file_plot_draft"));
        fit();
    }
    else if (state == FilePreviewLoader::State::Failed)
    {
        const auto error = presentError((m_executionSource ? m_executionSource : m_loader->result())->failure);
        m_message->setText(error.summary);
        m_message->setToolTip(error.details);
    }
    else
        m_message->setText(text(state == FilePreviewLoader::State::Loading ? "ui.file_plot_loading"
                               : state == FilePreviewLoader::State::Waiting ? "ui.file_plot_waiting"
                                                                          : "ui.file_plot_empty"));
    auto palette = m_message->palette();
    palette.setColor(QPalette::WindowText, themeColor(state == FilePreviewLoader::State::Failed
                                                       ? "error" : "muted"));
    m_message->setPalette(palette);
}
void FilePreviewPanel::fit()
{
    if (m_curve->data()->isEmpty()) return;
    m_curve->rescaleAxes();
    m_plot->xAxis->scaleRange(1.06);
    m_plot->yAxis->scaleRange(1.12);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}
void FilePreviewPanel::applyColors()
{
    auto palette = m_message->palette();
    palette.setColor(QPalette::WindowText, themeColor(m_loader->state() == FilePreviewLoader::State::Failed
                                                       ? "error" : "muted"));
    m_message->setPalette(palette);
    m_plot->setBackground(themeColor("plot"));
    for (auto *axis : {m_plot->xAxis, m_plot->yAxis})
    {
        axis->setBasePen(QPen(themeColor("axis")));
        axis->setTickPen(QPen(themeColor("axis")));
        axis->setSubTickPen(QPen(themeColor("axis")));
        axis->setTickLabelColor(themeColor("plotText"));
        axis->setLabelColor(themeColor("plotText"));
        axis->grid()->setPen(QPen(themeColor("grid"), 1, Qt::DotLine));
        axis->grid()->setZeroLinePen(QPen(themeColor("axis"), 1, Qt::DotLine));
        axis->setTickLabelFont(font());
        axis->setLabelFont(font());
    }
    m_curve->setPen(QPen(themeColor("curve"), 1.5));
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}
} // namespace aster::ui
