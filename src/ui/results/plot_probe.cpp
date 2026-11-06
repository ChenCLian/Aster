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
    layout->setContentsMargins(10, 8, 10, 9);
    layout->setSpacing(5);
    auto *header = new QHBoxLayout;
    m_title = new QLabel;
    m_title->setObjectName("plotProbeTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setMinimumWidth(0);
    header->addWidget(m_title, 1);
    auto *close = new QToolButton;
    close->setObjectName("closePlotProbe");
    close->setText(QString::fromUtf8("×"));
    close->setAccessibleName(text("ui.probe_close"));
    close->setToolTip(text("ui.probe_close"));
    header->addWidget(close);
    layout->addLayout(header);
    m_details = new QLabel;
    m_details->setObjectName("plotProbeDetails");
    m_details->setTextFormat(Qt::PlainText);
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_scroll = new QScrollArea;
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidget(m_details);
    layout->addWidget(m_scroll);
    auto *copy = new QToolButton;
    copy->setObjectName("copyPlotProbe");
    copy->setText(text("ui.probe_copy"));
    layout->addWidget(copy, 0, Qt::AlignRight);
    connect(copy, &QToolButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_title->toolTip() + "\n" + m_details->text());
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
    if (event->type() == QEvent::MouseButtonPress)
    {
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
    if (!m_plot->axisRect()->rect().contains(point.toPoint()) ||
        (m_plot->legend->visible() && m_plot->legend->outerRect().contains(point.toPoint()))) return;
    const auto sample = m_picker(point);
    if (!sample) return;
    const int width = std::min(300, m_plot->width() - 12);
    if (width < 120) return;
    m_bubble->setFixedWidth(width);
    m_title->setText(m_title->fontMetrics().elidedText(sample->title, Qt::ElideRight, width - 64));
    m_title->setToolTip(sample->title);
    m_details->setText(sample->details);
    m_details->setMinimumWidth(0);
    const int bodyHeight = std::max(m_details->fontMetrics().height(), m_details->heightForWidth(width - 36));
    m_scroll->setFixedHeight(std::min(bodyHeight + 4, std::max(24, m_plot->height() - 100)));
    m_bubble->adjustSize();
    const int height = m_bubble->sizeHint().height();
    if (height > m_plot->height() - 12) return; // A collapsed plot cannot contain a readable bubble.
    m_bubble->resize(width, height);
    auto anchor = sample->pixel.toPoint();
    int x = anchor.x() + 16, y = anchor.y() + 16;
    if (x + width > m_plot->width() - 6) x = anchor.x() - width - 16;
    if (y + height > m_plot->height() - 6) y = anchor.y() - height - 16;
    m_bubble->move(std::clamp(x, 6, std::max(6, m_plot->width() - width - 6)),
                   std::clamp(y, 6, std::max(6, m_plot->height() - height - 6)));
    m_marker->move(anchor - QPoint(6, 6));
    m_marker->show(); m_marker->raise();
    m_bubble->show(); m_bubble->raise();
}
}
