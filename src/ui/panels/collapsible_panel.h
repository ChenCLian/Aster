#ifndef ASTER_UI_COLLAPSIBLE_PANEL_H
#define ASTER_UI_COLLAPSIBLE_PANEL_H
#include <QWidget>
#include <QPointer>
class QAbstractButton;
namespace aster::ui
{
// Horizontal splitter pane with a persistent click/drag edge grip.
class CollapsiblePanel : public QWidget
{
    Q_OBJECT
  public:
    CollapsiblePanel(const QString &title, QWidget *content, int preferredWidth = 270,
                     QWidget *parent = nullptr);
    bool isCollapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed);
    // The host chooses which pane yields space; no main-window column indexes here.
    void setResizeTarget(QWidget *target) { m_resizeTarget = target; }
    QSize sizeHint() const override;
    void setExpandedWidthRange(int minimum, int maximum);
    void setGripOnRight(bool right);
  signals:
    void collapsedChanged(bool collapsed);
  protected:
    void resizeEvent(QResizeEvent *event) override;
  private:
    void dragEdge(int delta);
    QWidget *m_body;
    QAbstractButton *m_grip;
    QPointer<QWidget> m_resizeTarget;
    QString m_title;
    int m_openWidth;
    int m_openMinimum = OpenMinimumWidth, m_openMaximum = QWIDGETSIZE_MAX;
    bool m_collapsed = false, m_gripOnRight = false;
    static constexpr int GripWidth = 28;
    static constexpr int OpenMinimumWidth = 220;
};
} // namespace aster::ui
#endif
