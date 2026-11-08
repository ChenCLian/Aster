// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/results/plot_probe.h"
#include "ui/common/presentation.h"
#include "third_party/qcustomplot/qcustomplot.h"
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QShortcut>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QToolTip>
#include <limits>
#include <algorithm>
#include <utility>
namespace aster::ui
{
PlotProbe::PlotProbe(QCustomPlot *plot, Picker picker)
    : QObject(plot), m_plot(plot), m_picker(std::move(picker))
{
    m_bubble = new QFrame(plot);
    m_bubble->setObjectName("plotProbeBubble");
    auto *layout = new QVBoxLayout(m_bubble);
    layout->setContentsMargins(10, 7, 10, 8);
    layout->setSpacing(6);
    auto *header = new QHBoxLayout;
    header->setSpacing(6);
    m_swatch = new QLabel;
    m_swatch->setObjectName("plotProbeSwatch");
    m_swatch->setFixedSize(20, 14);
    header->addWidget(m_swatch);
    m_title = new QLabel;
    m_title->setObjectName("plotProbeTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setMinimumWidth(0);
    header->addWidget(m_title, 1);
    auto *copy = new QToolButton;
    copy->setObjectName("copyPlotProbe");
    copy->setText(text("ui.probe_copy_short"));
    copy->setToolTip(text("ui.probe_copy"));
    copy->setAccessibleName(text("ui.probe_copy"));
    header->addWidget(copy);
    auto *close = new QToolButton;
    close->setObjectName("closePlotProbe");
    close->setText(QString::fromUtf8("×"));
    close->setAccessibleName(text("ui.probe_close"));
    close->setToolTip(text("ui.probe_close"));
    header->addWidget(close);
    layout->addLayout(header);
    m_details = new QWidget;
    m_details->setObjectName("plotProbeDetails");
    auto *fields = new QGridLayout(m_details);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setHorizontalSpacing(12);
    fields->setVerticalSpacing(4);
    fields->setColumnStretch(0, 1);
    m_scroll = new QScrollArea;
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidget(m_details);
    layout->addWidget(m_scroll);
    connect(copy, &QToolButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_copyText);
    });
    connect(close, &QToolButton::clicked, this, [this] { dismiss(); });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), plot);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { dismiss(); });
    m_marker = new QFrame(plot);
    m_marker->setObjectName("plotProbeMarker");
    m_marker->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_marker->setFixedSize(12, 12);
    dismiss();
    plot->installEventFilter(this);
    // Range changes cover wheel, drag, fit and programmatic changes. No stale anchor.
    for (auto *axis : {plot->xAxis, plot->yAxis})
        connect(axis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged), this,
                [this] { dismiss(); });
}
void PlotProbe::dismiss()
{
    m_bubble->hide();
    m_marker->hide();
}
bool PlotProbe::eventFilter(QObject *object, QEvent *event)
{
    if (object != m_plot) return false;
    if (event->type() == QEvent::ToolTip && m_bubble->isVisible())
    {
        QToolTip::hideText();
        event->accept();
        return true; // Neither plot nor axis help may cover an active readout.
    }
    if (event->type() == QEvent::MouseButtonPress)
    {
        QToolTip::hideText();
        auto *mouse = static_cast<QMouseEvent *>(event);
        m_pressValid = mouse->button() == Qt::LeftButton;
        m_pressed = mouse->position();
        m_dragged = false;
        if (!m_pressValid) dismiss();
    }
    else if (event->type() == QEvent::MouseMove && m_pressValid)
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if ((mouse->position() - m_pressed).manhattanLength() >= QApplication::startDragDistance())
        { m_dragged = true; dismiss(); }
    }
    else if (event->type() == QEvent::MouseButtonRelease)
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const bool click = m_pressValid && !m_dragged && mouse->button() == Qt::LeftButton &&
            (mouse->position() - m_pressed).manhattanLength() < QApplication::startDragDistance();
        m_pressValid = false;
        if (click) inspect(mouse->position());
    }
    else if (event->type() == QEvent::Resize || event->type() == QEvent::Hide ||
             event->type() == QEvent::Wheel || event->type() == QEvent::MouseButtonDblClick)
    { m_pressValid = false; dismiss(); }
    return false; // Preserve QCustomPlot's normal pan/zoom and context menu behavior.
}
void PlotProbe::inspect(QPointF point)
{
    dismiss();
    QToolTip::hideText();
    if (!m_plot->axisRect()->rect().contains(point.toPoint()) ||
        (m_plot->legend->visible() && m_plot->legend->outerRect().contains(point.toPoint()))) return;
    const auto sample = m_picker(point);
    if (!sample) return;
    auto *fields = static_cast<QGridLayout *>(m_details->layout());
    while (auto *item = fields->takeAt(0)) { delete item->widget(); delete item; }
    m_copyText = sample->title;
    int labelWidth = 0, valueWidth = 0;
    for (int row = 0; row < sample->values.size(); ++row)
    {
        const auto &field = sample->values[row];
        auto *name = new QLabel(field.label);
        name->setObjectName("plotProbeFieldName");
        name->setTextFormat(Qt::PlainText);
        name->setWordWrap(true);
        name->setMinimumWidth(0);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *value = new QLabel(QString::number(field.value, 'g', 8));
        value->setObjectName("plotProbeValue");
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setAlignment(Qt::AlignRight | Qt::AlignTop);
        fields->addWidget(name, row, 0, Qt::AlignTop);
        fields->addWidget(value, row, 1, Qt::AlignTop);
        labelWidth = std::max(labelWidth, name->fontMetrics().horizontalAdvance(field.label));
        valueWidth = std::max(valueWidth, value->sizeHint().width());
        m_copyText += "\n" + field.label + ": " +
            QString::number(field.value, 'g', std::numeric_limits<double>::max_digits10);
    }
    if (!sample->note.isEmpty())
    {
        auto *note = new QLabel(sample->note);
        note->setObjectName("Muted");
        note->setTextFormat(Qt::PlainText);
        note->setWordWrap(true);
        fields->addWidget(note, sample->values.size(), 0, 1, 2);
        m_copyText += "\n" + sample->note;
    }
    int reserved = 20 + 18 + (sample->pen ? 26 : 0);
    for (const auto *button : m_bubble->findChildren<QToolButton *>())
        reserved += button->sizeHint().width();
    const int headerWidth = std::min(160, m_title->fontMetrics().horizontalAdvance(sample->title)) + reserved;
    const int preferred = std::clamp(std::max(headerWidth, std::min(labelWidth, 200) + valueWidth + 36), 230, 340);
    const int width = std::min(preferred, m_plot->width() - 12);
    if (width < 120) return;
    m_bubble->setFixedWidth(width);
    m_swatch->setVisible(sample->pen.has_value());
    if (sample->pen)
    {
        const qreal scale = m_plot->devicePixelRatioF();
        QPixmap icon(QSize(20, 14) * scale); icon.setDevicePixelRatio(scale); icon.fill(Qt::transparent);
        QPainter painter(&icon); painter.setRenderHint(QPainter::Antialiasing);
        auto pen = *sample->pen; pen.setWidthF(std::clamp(pen.widthF(), 1.5, 3.0));
        if (pen.style() == Qt::NoPen) pen.setStyle(Qt::SolidLine);
        painter.setPen(pen); painter.drawLine(QPointF(1, 7), QPointF(19, 7));
        painter.end(); m_swatch->setPixmap(icon);
        m_marker->setStyleSheet(QString("background: %1; border: 2px solid palette(base); border-radius: 6px;")
                                   .arg(pen.color().name(QColor::HexArgb)));
    }
    else m_marker->setStyleSheet({});
    m_title->setText(m_title->fontMetrics().elidedText(sample->title, Qt::ElideRight, std::max(20, width - reserved)));
    m_title->setToolTip(m_title->text() == sample->title ? QString() : sample->title);
    m_title->setAccessibleName(sample->title);
    const int bodyHeight = std::max(fields->minimumSize().height(), fields->heightForWidth(width - 24));
    m_scroll->setFixedHeight(std::min(bodyHeight + 4, std::max(24, m_plot->height() - 100)));
    m_bubble->adjustSize();
    const int height = m_bubble->sizeHint().height();
    if (height > m_plot->height() - 12) return; // A collapsed plot cannot contain a readable bubble.
    m_bubble->resize(width, height);
    auto anchor = sample->pixel.toPoint();
    int x = anchor.x() + 16, y = anchor.y() + 16;
    if (x + width > m_plot->width() - 6) x = anchor.x() - width - 16;
    if (y + height > m_plot->height() - 6) y = anchor.y() - height - 16;
    const auto bounded = [&](int px, int py) {
        return QRect(std::clamp(px, 6, std::max(6, m_plot->width() - width - 6)),
                     std::clamp(py, 6, std::max(6, m_plot->height() - height - 6)), width, height);
    };
    QRect placement = bounded(x, y);
    const QRect marker(anchor - QPoint(9, 9), QSize(18, 18));
    auto cost = [&](const QRect &r) {
        const auto overlap = m_plot->legend->visible() ? r.intersected(m_plot->legend->outerRect()) : QRect();
        return (r.intersects(marker) ? 1000000 : 0) + overlap.width() * overlap.height();
    };
    for (int px : {anchor.x() + 16, anchor.x() - width - 16})
        for (int py : {anchor.y() + 16, anchor.y() - height - 16})
        {
            const auto candidate = bounded(px, py);
            if (cost(candidate) < cost(placement)) placement = candidate;
        }
    m_bubble->move(placement.topLeft());
    m_marker->move(anchor - QPoint(6, 6));
    m_marker->show(); m_marker->raise();
    m_bubble->show(); m_bubble->raise();
}
}
