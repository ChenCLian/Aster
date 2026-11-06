// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/panels/collapsible_panel.h"
#include "ui/common/presentation.h"
#include "ui/settings/theme_manager.h"
#include <QAbstractButton>
#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
namespace aster::ui
{
namespace
{
class EdgeGrip : public QAbstractButton
{
  public:
    explicit EdgeGrip(QWidget *parent) : QAbstractButton(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::SizeHorCursor);
        setCheckable(true);
    }
    std::function<void(int)> dragged;
  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(themeColor(hasFocus() ? "accent" : "border"), 1));
        painter.setBrush(themeColor(underMouse() || isDown() ? "selection" : "surface"));
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 13, 13);
        painter.setPen(QPen(themeColor("accent"), 1.8, Qt::SolidLine, Qt::RoundCap));
        const qreal x = width() / 2.0, y = 15;
        const qreal direction = (isChecked() ? -1 : 1) * (property("rightEdge").toBool() ? -1 : 1);
        painter.drawPolyline(QPolygonF{QPointF(x - direction * 3, y - 4),
                                       QPointF(x + direction * 2, y),
                                       QPointF(x - direction * 3, y + 4)});
        painter.translate(width() / 2.0, height() / 2.0 + 10);
        painter.rotate(-90);
        painter.setPen(themeColor("muted"));
        auto font = painter.font();
        font.setPointSizeF(8.5);
        painter.setFont(font);
        painter.drawText(QRectF(-height() / 2.0 + 20, -width() / 2.0,
                                height() - 40, width()), Qt::AlignCenter, text());
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        m_start = m_last = event->globalPosition().toPoint();
        m_dragging = false;
        QAbstractButton::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton)
        {
            const auto point = event->globalPosition().toPoint();
            if (!m_dragging && (point - m_start).manhattanLength() >= QApplication::startDragDistance())
                m_dragging = true;
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
            setDown(false);
            event->accept();
            return;
        }
        QAbstractButton::mouseReleaseEvent(event);
    }
  private:
    QPoint m_start, m_last;
    bool m_dragging = false;
};
} // namespace
CollapsiblePanel::CollapsiblePanel(const QString &title, QWidget *content, int preferredWidth,
                                  QWidget *parent)
    : QWidget(parent), m_title(title), m_openWidth(std::max(OpenMinimumWidth, preferredWidth))
{
    setMinimumWidth(OpenMinimumWidth);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(GripWidth, 0, 0, 0);
    m_body = new QFrame;
    m_body->setObjectName("Panel");
    auto *bodyLayout = new QVBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    auto *label = new QLabel(title);
    label->setObjectName("SidePanelTitle");
    label->setWordWrap(true);
    bodyLayout->addWidget(label);
    bodyLayout->addWidget(content, 1);
    layout->addWidget(m_body);
    auto *grip = new EdgeGrip(this);
    m_grip = grip;
    m_grip->setObjectName("sidePanelGrip");
    m_grip->setText(title);
    m_grip->setAccessibleName(title);
    m_grip->setToolTip(ui::text("ui.panel_grip_hint").arg(title));
    connect(m_grip, &QAbstractButton::clicked, this, [this] { setCollapsed(!m_collapsed); });
    grip->dragged = [this](int delta) { dragEdge(delta); };
    connect(&ThemeManager::instance(), &ThemeManager::changed, m_grip, qOverload<>(&QWidget::update));
}
void CollapsiblePanel::setToggleShortcut(const QKeySequence &shortcut)
{
    m_grip->setShortcut(shortcut);
    const auto action = shortcutLabel(text("ui.toggle_panel").arg(m_title), shortcut);
    m_grip->setToolTip(action + "\n" + text("ui.panel_grip_hint").arg(m_title));
    m_grip->setAccessibleDescription(action);
}
void CollapsiblePanel::setExpandedWidthRange(int minimum, int maximum)
{
    m_openMinimum = std::max(GripWidth, minimum);
    m_openMaximum = std::max(m_openMinimum, maximum);
    m_openWidth = std::clamp(m_openWidth, m_openMinimum, m_openMaximum);
    if (!m_collapsed) { setMinimumWidth(m_openMinimum); setMaximumWidth(m_openMaximum); }
}
QSize CollapsiblePanel::sizeHint() const
{
    return {m_collapsed ? GripWidth : m_openWidth, 400};
}
void CollapsiblePanel::setGripOnRight(bool right)
{
    m_gripOnRight = right;
    m_grip->setProperty("rightEdge", right);
    layout()->setContentsMargins(right ? 0 : GripWidth, 0, right ? GripWidth : 0, 0);
    const int h = std::min(132, height());
    m_grip->setGeometry(right ? width() - GripWidth : 0, std::max(0, (height() - h) / 2), GripWidth, h);
    m_grip->update();
}
void CollapsiblePanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int h = std::min(132, height());
    m_grip->setGeometry(m_gripOnRight ? width() - GripWidth : 0, std::max(0, (height() - h) / 2), GripWidth, h);
    m_grip->raise();
}
void CollapsiblePanel::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed) return;
    auto *splitter = qobject_cast<QSplitter *>(parentWidget());
    auto sizes = splitter ? splitter->sizes() : QList<int>{};
    const int index = splitter ? splitter->indexOf(this) : -1;
    if (collapsed) m_openWidth = std::max(m_openMinimum, width());
    m_collapsed = collapsed;
    m_body->setVisible(!collapsed);
    m_grip->setChecked(collapsed);
    setMinimumWidth(collapsed ? GripWidth : m_openMinimum);
    setMaximumWidth(collapsed ? GripWidth : m_openMaximum);
    if (index >= 0 && sizes.size() > 1)
    {
        const int target = collapsed ? GripWidth : m_openWidth;
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
    if (m_collapsed) setCollapsed(false);
    auto *splitter = qobject_cast<QSplitter *>(parentWidget());
    if (!splitter) return;
    const int index = splitter->indexOf(this);
    if (index < 0) return;
    int neighbor = splitter->indexOf(m_resizeTarget);
    if (neighbor < 0 || neighbor == index) neighbor = index - 1;
    if (neighbor < 0) return;
    auto sizes = splitter->sizes();
    const int lower = minimumWidth() - sizes[index];
    const int upper = std::min(maximumWidth() - sizes[index], sizes[neighbor] - splitter->widget(neighbor)->minimumWidth());
    const int growth = std::clamp(m_gripOnRight ? delta : -delta, lower, std::max(lower, upper));
    sizes[index] += growth;
    sizes[neighbor] -= growth;
    splitter->setSizes(sizes);
}
} // namespace aster::ui
