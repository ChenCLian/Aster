#include "ui/panels/result_export_panel.h"
#include "ui/common/presentation.h"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QVBoxLayout>
namespace aster::ui
{
ResultExportPanel::ResultExportPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    content->setObjectName("Surface");
    auto *fields = new QVBoxLayout(content);
    fields->setContentsMargins(8, 8, 8, 8);
    fields->setSpacing(10);
    const auto label = [fields](const QString &value, bool heading = false)
    {
        auto *item = new QLabel(value);
        item->setTextFormat(Qt::PlainText);
        item->setWordWrap(true);
        item->setMinimumWidth(0);
        item->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        item->setObjectName(heading ? "SectionTitle" : "Muted");
        fields->addWidget(item);
        return item;
    };
    label(text("ui.export_data_heading"), true);
    m_dataTarget = label({});
    m_dataTarget->setObjectName("exportDataTarget");
    m_ports = new QComboBox;
    m_ports->setObjectName("outputPort");
    m_ports->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_ports->setMinimumContentsLength(6);
    m_ports->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    fields->addWidget(m_ports);
    m_csv = new QPushButton(text("ui.export_csv_local"));
    m_csv->setToolTip(text("ui.export_data_help"));
    m_csv->setObjectName("exportCsv");
    fields->addWidget(m_csv);
    m_reuse = new QPushButton(text("ui.use_output_as_source"));
    m_reuse->setObjectName("reuseOutput");
    fields->addWidget(m_reuse);
    fields->addSpacing(14);
    label(text("ui.export_figure_heading"), true);
    m_figureTarget = label({});
    m_figureTarget->setObjectName("exportFigureTarget");
    m_figure = new QPushButton(text("ui.export_figure_local"));
    m_figure->setToolTip(text("ui.export_figure_help"));
    m_figure->setObjectName("exportPng");
    fields->addWidget(m_figure);
    fields->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setObjectName("exportStatus");
    m_status->hide();
    layout->addWidget(m_status);
    m_progress = new QProgressBar;
    m_progress->setObjectName("exportProgress");
    m_progress->hide();
    layout->addWidget(m_progress);
    m_cancel = new QPushButton(text("ui.cancel_export"));
    m_cancel->setObjectName("cancelExport");
    m_cancel->hide();
    layout->addWidget(m_cancel);
    connect(m_csv, &QPushButton::clicked, this, &ResultExportPanel::exportDataRequested);
    connect(m_figure, &QPushButton::clicked, this, &ResultExportPanel::exportFigureRequested);
    connect(m_reuse, &QPushButton::clicked, this, &ResultExportPanel::reuseRequested);
    connect(m_cancel, &QPushButton::clicked, this, &ResultExportPanel::cancelRequested);
    connect(m_ports, qOverload<int>(&QComboBox::activated), this,
            [this] { emit outputActivated(); });
    updateActions();
}
void ResultExportPanel::setTargets(const QString &dataTarget, bool hasData,
                                   const QString &figureTarget, bool hasFigure)
{
    m_dataTarget->setText(dataTarget);
    m_figureTarget->setText(figureTarget);
    m_hasData = hasData;
    m_hasFigure = hasFigure;
    updateActions();
}
void ResultExportPanel::setBusy(bool busy, bool figureExport)
{
    m_busy = busy;
    m_progress->setRange(0, figureExport ? 0 : 100);
    if (busy) m_progress->setValue(0);
    m_progress->setVisible(busy);
    m_cancel->setVisible(busy);
    updateActions();
}
void ResultExportPanel::setProgress(int value) { m_progress->setValue(value); }
void ResultExportPanel::setStatus(const QString &message)
{
    m_status->setText(message);
    m_status->setVisible(!message.isEmpty());
}
void ResultExportPanel::updateActions()
{
    m_ports->setEnabled(m_ports->count() > 0 && !m_busy);
    m_csv->setEnabled(m_hasData && !m_busy);
    m_reuse->setEnabled(m_hasData && !m_busy);
    m_figure->setEnabled(m_hasFigure && !m_busy);
}
} // namespace aster::ui
