// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_COLLAPSIBLE_PANEL_H
#define ASTER_UI_COLLAPSIBLE_PANEL_H
#include <QWidget>
#include <QPointer>
#include <QKeySequence>
class QAbstractButton;
class QVBoxLayout;
namespace aster::ui
{
class PanelControlRail : public QWidget
{
  public:
    explicit PanelControlRail(QWidget *parent = nullptr);
    void addButton(QAbstractButton *button);
  private:
    QVBoxLayout *m_layout;
};

// A task pane and its persistent click/drag control.
class CollapsiblePanel : public QWidget
{
    Q_OBJECT
  public:
    enum class Kind { Preview, Settings, Catalog };
    CollapsiblePanel(Kind kind, const QString &title, QWidget *content, int preferredWidth,
                     PanelControlRail &controls);
    QAbstractButton *toggleButton() const { return m_grip; }
    bool isCollapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed);
    void setToggleShortcut(const QKeySequence &shortcut);
    // The host chooses which panes can yield space when the grip is dragged.
    void setResizeTarget(QWidget *target, QWidget *fallback = nullptr)
    {
        m_resizeTarget = target;
        m_resizeFallback = fallback;
    }
    QSize sizeHint() const override;
    void setExpandedWidthRange(int minimum, int maximum);
  signals:
    void collapsedChanged(bool collapsed);
  private:
    void dragEdge(int delta);
    void updateGrip();
    Kind m_kind;
    QAbstractButton *m_grip;
    QPointer<QWidget> m_resizeTarget, m_resizeFallback;
    QString m_title;
    int m_openWidth;
    int m_openMinimum = OpenMinimumWidth, m_openMaximum = QWIDGETSIZE_MAX;
    bool m_collapsed = false;
    static constexpr int GripSize = 36;
    static constexpr int OpenMinimumWidth = 220;
};
} // namespace aster::ui
#endif
