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
#include <array>
namespace aster::ui
{
struct PlotProbe::Entry
{
    ProbeSample sample;
    QFrame *bubble = nullptr, *marker = nullptr;
    QLabel *title = nullptr, *swatch = nullptr;
    QWidget *details = nullptr;
    QScrollArea *scroll = nullptr;
    QString copyText;
    std::optional<QPoint> manualOffset;
    QSize layoutViewport;
};
PlotProbe::PlotProbe(QCustomPlot *plot, Picker picker, Projector projector)
    : QObject(plot), m_plot(plot), m_picker(std::move(picker)), m_projector(std::move(projector))
{
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(0);
    connect(&m_refresh, &QTimer::timeout, this, &PlotProbe::refresh);
    connect(plot, &QCustomPlot::afterReplot, this, [this] { m_refresh.start(); });
    plot->installEventFilter(this);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), plot);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { if (m_active) close(m_active); });
}
PlotProbe::~PlotProbe()
{
    // Never notify owners while the plot/widget hierarchy is being destroyed.
    blockSignals(true);
    m_plot->removeEventFilter(this);
    dismiss();
}
void PlotProbe::dismiss()
{
    m_refresh.stop();
    if (m_entries.isEmpty()) return;
    const bool wasBlocked = blockSignals(true);
    while (!m_entries.isEmpty()) close(m_entries.back());
    blockSignals(wasBlocked);
    emit changed();
}
void PlotProbe::close(Entry *entry)
{
    m_entries.removeOne(entry);
    if (m_dragging == entry) m_dragging = nullptr;
    if (m_active == entry) m_active = m_entries.isEmpty() ? nullptr : m_entries.back();
    entry->bubble->hide(); entry->marker->hide();
    // A deferred widget deletion must not leave callbacks to a freed Entry.
    for (auto *button : entry->bubble->findChildren<QToolButton *>())
        disconnect(button, nullptr, this, nullptr);
    entry->bubble->deleteLater(); entry->marker->deleteLater();
    delete entry;
    emit changed();
}
void PlotProbe::refresh()
{
    for (auto *entry : m_entries) place(*entry);
}
bool PlotProbe::eventFilter(QObject *object, QEvent *event)
{
    if (object != m_plot) {
        auto found = std::find_if(m_entries.begin(), m_entries.end(), [object](Entry *e) {
            return object == e->bubble || object == e->title || object == e->swatch;
        });
        if (found == m_entries.end()) return false;
        auto *entry = *found;
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_active = m_dragging = entry;
                m_dragOffset = m_plot->mapFromGlobal(mouse->globalPosition().toPoint()) - entry->bubble->pos();
                entry->bubble->raise(); return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging == entry) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            entry->manualOffset = m_plot->mapFromGlobal(mouse->globalPosition().toPoint()) - m_dragOffset - entry->sample.pixel.toPoint();
            place(*entry); return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_dragging == entry) {
            m_dragging = nullptr; return true;
        }
        return false;
    }
    if (event->type() == QEvent::ToolTip && hasBubbles()) {
        QToolTip::hideText(); event->accept(); return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        QToolTip::hideText();
        auto *mouse = static_cast<QMouseEvent *>(event);
        m_pressValid = mouse->button() == Qt::LeftButton;
        m_pressed = mouse->position(); m_dragged = false;
    } else if (event->type() == QEvent::MouseMove && m_pressValid) {
        if ((static_cast<QMouseEvent *>(event)->position() - m_pressed).manhattanLength() >= QApplication::startDragDistance()) m_dragged = true;
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const bool click = m_pressValid && !m_dragged && mouse->button() == Qt::LeftButton &&
            (mouse->position() - m_pressed).manhattanLength() < QApplication::startDragDistance();
        m_pressValid = false;
        if (click) inspect(mouse->position());
    } else if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        m_refresh.start();
    } else if (event->type() == QEvent::Wheel || event->type() == QEvent::Hide) m_pressValid = false;
    return false;
}
void PlotProbe::inspect(QPointF point)
{
    QToolTip::hideText();
    if (!m_plot->axisRect()->rect().contains(point.toPoint()) ||
        (m_plot->legend->visible() && m_plot->legend->outerRect().contains(point.toPoint()))) return;
    const auto sample = m_picker(point);
    if (!sample) return; // Blank clicks never remove pinned readings.
    for (auto *entry : m_entries) {
        if (entry->sample.key == sample->key && entry->sample.coordinates == sample->coordinates) {
            entry->sample = *sample; populate(*entry); place(*entry);
            m_active = entry; entry->bubble->raise(); return;
        }
    }
    if (m_entries.size()>=128) {
        QToolTip::showText(m_plot->mapToGlobal(point.toPoint()),text("ui.probe_limit"),m_plot);
        return;
    }
    auto *created = new Entry;
    auto &entry = *created;
    entry.sample = *sample;
    entry.bubble = new QFrame(m_plot);
    entry.bubble->setObjectName("plotProbeBubble");
    auto *layout = new QVBoxLayout(entry.bubble);
    layout->setContentsMargins(10, 7, 10, 8);
    layout->setSpacing(6);
    auto *header = new QHBoxLayout;
    header->setSpacing(6);
    entry.swatch = new QLabel;
    entry.swatch->setObjectName("plotProbeSwatch");
    entry.swatch->setFixedSize(20, 14);
    header->addWidget(entry.swatch);
    entry.title = new QLabel;
    entry.title->setObjectName("plotProbeTitle");
    entry.title->setTextFormat(Qt::PlainText);
    entry.title->setMinimumWidth(0);
    header->addWidget(entry.title, 1);
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
    close->setFixedSize(28, 28);
    auto closeFont = close->font(); closeFont.setPointSizeF(16); close->setFont(closeFont);
    header->addWidget(close);
    layout->addLayout(header);
    entry.details = new QWidget;
    entry.details->setObjectName("plotProbeDetails");
    auto *fields = new QGridLayout(entry.details);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setHorizontalSpacing(12);
    fields->setVerticalSpacing(4);
    fields->setColumnStretch(0, 1);
    entry.scroll = new QScrollArea;
    entry.scroll->setFrameShape(QFrame::NoFrame);
    entry.scroll->setWidgetResizable(true);
    entry.scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    entry.scroll->setWidget(entry.details);
    layout->addWidget(entry.scroll);
    entry.marker = new QFrame(m_plot);
    entry.marker->setObjectName("plotProbeMarker");
    entry.marker->setAttribute(Qt::WA_TransparentForMouseEvents);
    entry.marker->setFixedSize(12, 12);
    for (QObject *target : std::array<QObject *, 3>{entry.bubble, entry.title, entry.swatch}) target->installEventFilter(this);
    connect(copy, &QToolButton::clicked, this, [this, created] {
        if (m_entries.contains(created)) QApplication::clipboard()->setText(created->copyText);
    });
    connect(close, &QToolButton::clicked, this, [this, created] {
        if (m_entries.contains(created)) this->close(created);
    });
    m_entries.push_back(created); m_active = created;
    populate(entry); place(entry); entry.bubble->raise(); emit changed();
}
void PlotProbe::populate(Entry &entry)
{
    auto *fields = static_cast<QGridLayout *>(entry.details->layout());
    while (auto *item = fields->takeAt(0)) { delete item->widget(); delete item; }
    entry.copyText = entry.sample.title;
    for (int row = 0; row < entry.sample.values.size(); ++row)
    {
        const auto &field = entry.sample.values[row];
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
        // A refreshed card is already visible. Include its newly inserted
        // children in size calculations before the next event-loop layout.
        name->show(); value->show();
        entry.copyText += "\n" + field.label + ": " +
            QString::number(field.value, 'g', std::numeric_limits<double>::max_digits10);
    }
    if (!entry.sample.note.isEmpty())
    {
        auto *note = new QLabel(entry.sample.note);
        note->setObjectName("Muted");
        note->setTextFormat(Qt::PlainText);
        note->setWordWrap(true);
        fields->addWidget(note, entry.sample.values.size(), 0, 1, 2);
        note->show();
        entry.copyText += "\n" + entry.sample.note;
    }
    relayout(entry);
}
void PlotProbe::relayout(Entry &entry)
{
    entry.layoutViewport = m_plot->size();
    auto *fields = static_cast<QGridLayout *>(entry.details->layout());
    int labelWidth = 0, valueWidth = 0;
    for (const auto *name : entry.details->findChildren<QLabel *>("plotProbeFieldName"))
        labelWidth = std::max(labelWidth, name->fontMetrics().horizontalAdvance(name->text()));
    for (const auto *value : entry.details->findChildren<QLabel *>("plotProbeValue"))
        valueWidth = std::max(valueWidth, value->sizeHint().width());
    int reserved = 20 + 18 + (entry.sample.pen ? 26 : 0);
    for (const auto *button : entry.bubble->findChildren<QToolButton *>())
        reserved += button->sizeHint().width();
    const int headerWidth = std::min(160, entry.title->fontMetrics().horizontalAdvance(entry.sample.title)) + reserved;
    const int preferred = std::clamp(std::max(headerWidth, std::min(labelWidth, 200) + valueWidth + 36), 230, 340);
    const int width = std::min(preferred, m_plot->width() - 12);
    if (width < 120) { entry.bubble->hide(); return; }
    entry.bubble->setFixedWidth(width);
    entry.swatch->setVisible(entry.sample.pen.has_value());
    if (entry.sample.pen)
    {
        const qreal scale = m_plot->devicePixelRatioF();
        QPixmap icon(QSize(20, 14) * scale); icon.setDevicePixelRatio(scale); icon.fill(Qt::transparent);
        QPainter painter(&icon); painter.setRenderHint(QPainter::Antialiasing);
        auto pen = *entry.sample.pen; pen.setWidthF(std::clamp(pen.widthF(), 1.5, 3.0));
        if (pen.style() == Qt::NoPen) pen.setStyle(Qt::SolidLine);
        painter.setPen(pen); painter.drawLine(QPointF(1, 7), QPointF(19, 7));
        painter.end(); entry.swatch->setPixmap(icon);
        entry.marker->setStyleSheet(QString("background: %1; border: 2px solid palette(base); border-radius: 6px;")
                                   .arg(pen.color().name(QColor::HexArgb)));
    }
    else entry.marker->setStyleSheet({});
    entry.title->setText(entry.title->fontMetrics().elidedText(entry.sample.title, Qt::ElideRight, std::max(20, width - reserved)));
    entry.title->setToolTip(entry.title->text() == entry.sample.title ? QString() : entry.sample.title);
    entry.title->setAccessibleName(entry.sample.title);
    const int bodyHeight = std::max(fields->minimumSize().height(), fields->heightForWidth(width - 24));
    entry.scroll->setFixedHeight(std::min(bodyHeight + 4, std::max(24, m_plot->height() - 100)));
    entry.bubble->adjustSize();
    const int height = entry.bubble->sizeHint().height();
    if (height > m_plot->height() - 12) { entry.bubble->hide(); return; } // A collapsed plot cannot contain a readable bubble.
    entry.bubble->resize(width, height);
}
void PlotProbe::place(Entry &entry)
{
    const auto pixel = m_projector(entry.sample);
    if (!pixel || !std::isfinite(pixel->x()) || !std::isfinite(pixel->y()) ||
        !m_plot->axisRect()->rect().contains(pixel->toPoint()) || m_plot->width() < 140 || m_plot->height() < 120) {
        entry.bubble->hide(); entry.marker->hide(); return;
    }
    entry.sample.pixel = *pixel;
    if (entry.layoutViewport != m_plot->size()) relayout(entry);
    const int width = entry.bubble->width(), height = entry.bubble->height();
    if (height > m_plot->height() - 12) { entry.bubble->hide(); entry.marker->hide(); return; }
    const auto anchor = pixel->toPoint();
    auto bounded = [&](QPoint pos) {
        return QRect(std::clamp(pos.x(), 6, std::max(6, m_plot->width()-width-6)),
                     std::clamp(pos.y(), 6, std::max(6, m_plot->height()-height-6)), width, height);
    };
    QRect placement = bounded(anchor + entry.manualOffset.value_or(QPoint(16, 16)));
    if (!entry.manualOffset) {
        auto cost = [&](QRect r) {
            qint64 result = 0;
            const auto area = [](QRect a) { return qint64(a.width()) * a.height(); };
            if (m_plot->legend->visible()) result += area(r.intersected(m_plot->legend->outerRect()));
            if (r.intersects(QRect(anchor-QPoint(9,9), QSize(18,18)))) result += 1000000;
            // Bounded local placement; no expensive global packing or data copies.
            for (auto *other : m_entries) if (other != &entry && !other->bubble->isHidden())
                result += area(r.intersected(other->bubble->geometry()));
            return result;
        };
        for (int dx : {16, -width-16}) for (int dy : {16, -height-16}) {
            const auto candidate = bounded(anchor + QPoint(dx,dy));
            if (cost(candidate) < cost(placement)) placement = candidate;
        }
    }
    entry.bubble->move(placement.topLeft());
    entry.marker->move(anchor-QPoint(6,6));
    entry.marker->show(); entry.bubble->show();
}
}
