// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/panels/collapsible_panel.h"
#include "ui/common/presentation.h"
#include "ui/settings/theme_manager.h"
#include <QAbstractButton>
#include <QApplication>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolTip>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
namespace aster::ui
{
namespace
{
QString panelPurpose(CollapsiblePanel::Kind kind)
{
    switch (kind)
    {
    case CollapsiblePanel::Kind::Preview: return text("ui.panel_preview_purpose");
    case CollapsiblePanel::Kind::Settings: return text("ui.panel_settings_purpose");
    case CollapsiblePanel::Kind::Catalog: return text("ui.panel_library_purpose");
    }
    Q_UNREACHABLE();
}

class PanelButton : public QAbstractButton
{
  public:
    PanelButton(CollapsiblePanel::Kind kind, QWidget *parent) : QAbstractButton(parent), m_kind(kind)
    {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setCheckable(true);
        setAttribute(Qt::WA_Hover);
        m_hintTimer.setSingleShot(true);
        m_hintTimer.setInterval(250);
        connect(&m_hintTimer, &QTimer::timeout, this, [this] {
            if (underMouse() && isVisible() && !QApplication::mouseButtons()) showHint();
        });
    }
    std::function<void(int)> dragged;
  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hover = underMouse();
        const bool open = isChecked();
        const bool keyboardFocus = hasFocus() && window()->testAttribute(Qt::WA_KeyboardFocusChange);
        auto fill = themeColor(open ? "muted" : "surface");
        if (isDown())
            fill = themeColor(open ? "accentPressed" : "border");
        else if (hover)
            fill = themeColor(open ? "accentFill" : "selection");
        const auto outline = open ? fill : themeColor(hover ? "accent" : "controlBorder");
        const auto ink = open ? themeColor("accentText") : themeColor(hover || keyboardFocus ? "text" : "muted");

        const QRectF face = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        painter.setPen(QPen(outline, 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(face, 5, 5);
        if (keyboardFocus)
        {
            painter.setPen(QPen(ink, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(face.adjusted(3, 3, -3, -3), 3, 3);
        }

        constexpr int iconSize = 20;
        painter.translate((width() - iconSize) / 2.0, (height() - iconSize) / 2.0);
        painter.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        switch (m_kind)
        {
        case CollapsiblePanel::Kind::Preview:
            painter.drawPolyline(QPolygonF{{1, 10}, {5, 10}, {8, 3}, {12, 17}, {15, 10}, {19, 10}});
            break;
        case CollapsiblePanel::Kind::Settings:
        {
            const auto drawSlider = [&painter](qreal x, qreal y) {
                painter.drawLine(QPointF(1, y), QPointF(x - 2.5, y));
                painter.drawLine(QPointF(x + 2.5, y), QPointF(19, y));
                painter.drawEllipse(QPointF(x, y), 2.5, 2.5);
            };
            drawSlider(7, 5.5);
            drawSlider(13, 14.5);
            break;
        }
        case CollapsiblePanel::Kind::Catalog:
            for (qreal y : {1.5, 11.5})
                for (qreal x : {1.5, 11.5})
                    painter.drawRoundedRect(QRectF(x, y, 7, 7), 1.2, 1.2);
            break;
        }
    }
    void enterEvent(QEnterEvent *event) override
    {
        QAbstractButton::enterEvent(event);
        m_hintTimer.start();
    }
    void leaveEvent(QEvent *event) override
    {
        m_hintTimer.stop();
        QAbstractButton::leaveEvent(event);
    }
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::ToolTip)
        {
            if (!isDown() && !m_dragging) showHint();
            return true;
        }
        if (event->type() == QEvent::ToolTipChange && hasFocus()
            && window()->testAttribute(Qt::WA_KeyboardFocusChange) && QToolTip::isVisible())
            showHint();
        return QAbstractButton::event(event);
    }
    void focusInEvent(QFocusEvent *event) override
    {
        QAbstractButton::focusInEvent(event);
        if (event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason)
            QTimer::singleShot(0, this, [this] {
                if (hasFocus() && isVisible()) showHint();
            });
    }
    void focusOutEvent(QFocusEvent *event) override
    {
        if (!underMouse() && QToolTip::text() == toolTip()) QToolTip::hideText();
        QAbstractButton::focusOutEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        m_hintTimer.stop();
        QToolTip::hideText();
        m_start = m_last = event->globalPosition().toPoint();
        m_dragging = false;
        QAbstractButton::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton)
        {
            const auto point = event->globalPosition().toPoint();
            if (!m_dragging && std::abs(point.x() - m_start.x()) >= QApplication::startDragDistance())
            {
                m_dragging = true;
                setCursor(Qt::SizeHorCursor);
                QToolTip::hideText();
            }
            if (m_dragging)
            {
                if (dragged) dragged(point.x() - m_last.x());
                m_last = point;
                event->accept();
                return;
            }
        }
        QAbstractButton::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_dragging)
        {
            m_dragging = false;
            setCursor(Qt::PointingHandCursor);
            setDown(false);
            event->accept();
            return;
        }
        QAbstractButton::mouseReleaseEvent(event);
    }
  private:
    void showHint()
    {
        const auto hint = toolTip();
        const QFontMetrics metrics(QToolTip::font());
        const QSize size = metrics.size(0, hint);
        // Leave room for the native tooltip padding and cursor offset.
        const int x = layoutDirection() == Qt::RightToLeft ? width() : -size.width() - 2 * metrics.height() - 12;
        QToolTip::showText(mapToGlobal(QPoint(x, -size.height() / 2)), hint, this, rect());
    }
    CollapsiblePanel::Kind m_kind;
    QTimer m_hintTimer;
    QPoint m_start, m_last;
    bool m_dragging = false;
};
} // namespace

PanelControlRail::PanelControlRail(QWidget *parent) : QWidget(parent), m_layout(new QVBoxLayout(this))
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(5);
}

void PanelControlRail::addButton(QAbstractButton *button)
{
    m_layout->addWidget(button);
}

CollapsiblePanel::CollapsiblePanel(Kind kind, const QString &title, QWidget *content, int preferredWidth,
                                  PanelControlRail &controls)
    : m_kind(kind), m_title(title), m_openWidth(std::max(OpenMinimumWidth, preferredWidth))
{
    setMinimumWidth(OpenMinimumWidth);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *body = new QFrame;
    body->setObjectName("Panel");
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    auto *label = new QLabel(title);
    label->setObjectName("SidePanelTitle");
    label->setWordWrap(true);
    bodyLayout->addWidget(label);
    bodyLayout->addWidget(content, 1);
    layout->addWidget(body);
    auto *grip = new PanelButton(kind, &controls);
    m_grip = grip;
    grip->setObjectName("sidePanelGrip");
    grip->setFixedSize(GripSize, GripSize);
    grip->setText(title);
    controls.addButton(grip);
    updateGrip();
    connect(grip, &QAbstractButton::clicked, this, [this] { setCollapsed(!m_collapsed); });
    grip->dragged = [panel = QPointer<CollapsiblePanel>(this)](int delta) {
        if (panel) panel->dragEdge(delta);
    };
    connect(this, &QObject::destroyed, grip, &QObject::deleteLater);
    connect(&ThemeManager::instance(), &ThemeManager::changed, grip, qOverload<>(&QWidget::update));
}
void CollapsiblePanel::setToggleShortcut(const QKeySequence &shortcut)
{
    m_grip->setShortcut(shortcut);
    updateGrip();
}
void CollapsiblePanel::updateGrip()
{
    const auto action = text(m_collapsed ? "ui.expand_panel" : "ui.collapse_panel").arg(m_title);
    const auto shortcut = m_grip->shortcut();
    const auto label = shortcut.isEmpty() ? action : shortcutLabel(action, shortcut);
    const auto tip = label + '\n' + panelPurpose(m_kind) + '\n' + text("ui.panel_grip_resize");
    m_grip->setChecked(!m_collapsed);
    m_grip->setAccessibleName(action);
    m_grip->setAccessibleDescription(tip);
    m_grip->setToolTip(tip);
}
void CollapsiblePanel::setExpandedWidthRange(int minimum, int maximum)
{
    m_openMinimum = std::max(1, minimum);
    m_openMaximum = std::max(m_openMinimum, maximum);
    m_openWidth = std::clamp(m_openWidth, m_openMinimum, m_openMaximum);
    setMinimumWidth(m_openMinimum);
    setMaximumWidth(m_openMaximum);
}
QSize CollapsiblePanel::sizeHint() const
{
    return {m_collapsed ? 0 : m_openWidth, 400};
}
void CollapsiblePanel::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed) return;
    auto *splitter = qobject_cast<QSplitter *>(parentWidget());
    auto sizes = splitter ? splitter->sizes() : QList<int>{};
    const int index = splitter ? splitter->indexOf(this) : -1;
    if (collapsed) m_openWidth = std::max(m_openMinimum, width());
    m_collapsed = collapsed;
    setVisible(!collapsed);
    updateGrip();
    if (index >= 0 && sizes.size() > 1)
    {
        const int target = collapsed ? 0 : m_openWidth;
        int recipient = splitter->indexOf(m_resizeTarget);
        if (recipient < 0 || recipient == index) recipient = index > 0 ? index - 1 : 1;
        sizes[recipient] = std::max(0, sizes[recipient] + sizes[index] - target);
        sizes[index] = target;
        splitter->setSizes(sizes);
    }
    updateGeometry();
    emit collapsedChanged(collapsed);
}
void CollapsiblePanel::dragEdge(int delta)
{
    const int requested = layoutDirection() == Qt::RightToLeft ? delta : -delta;
    if (m_collapsed)
    {
        if (requested <= 0) return;
        setCollapsed(false);
    }
    auto *splitter = qobject_cast<QSplitter *>(parentWidget());
    if (!splitter) return;
    const int index = splitter->indexOf(this);
    if (index < 0) return;
    int neighbor = splitter->indexOf(m_resizeTarget);
    if (neighbor < 0 || neighbor == index) neighbor = index - 1;
    if (neighbor < 0) return;
    auto sizes = splitter->sizes();
    const int lower = minimumWidth() - sizes[index];
    const int primaryRoom = std::max(0, sizes[neighbor] - splitter->widget(neighbor)->minimumWidth());
    const int fallback = m_resizeFallback ? splitter->indexOf(m_resizeFallback) : -1;
    const bool canUseFallback = fallback >= 0 && fallback != neighbor && fallback != index;
    const int fallbackRoom = canUseFallback ? std::max(0, sizes[fallback] - m_resizeFallback->minimumWidth()) : 0;
    const int upper = std::min(maximumWidth() - sizes[index], primaryRoom + fallbackRoom);
    const int growth = std::clamp(requested, lower, std::max(lower, upper));
    const int fromFallback = std::clamp(growth - primaryRoom, 0, fallbackRoom);
    sizes[index] += growth;
    sizes[neighbor] -= growth - fromFallback;
    if (fromFallback > 0) sizes[fallback] -= fromFallback;
    splitter->setSizes(sizes);
}
} // namespace aster::ui
