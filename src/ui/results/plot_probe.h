#ifndef ASTER_UI_RESULTS_PLOT_PROBE_H
#define ASTER_UI_RESULTS_PLOT_PROBE_H
#include <QObject>
#include <QPointF>
#include <QString>
#include <functional>
#include <optional>
class QCustomPlot;
class QFrame;
class QLabel;
class QScrollArea;
namespace aster::ui
{
// A value snapshot, never a pointer into a graph that refinement may delete.
struct ProbeSample
{
    QPointF pixel;
    QString title, details;
};
class PlotProbe final : public QObject
{
  public:
    using Picker = std::function<std::optional<ProbeSample>(QPointF)>;
    PlotProbe(QCustomPlot *plot, Picker picker);
    void dismiss();
  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
  private:
    void inspect(QPointF point);
    QCustomPlot *m_plot;
    Picker m_picker;
    QFrame *m_bubble, *m_marker;
    QLabel *m_title, *m_details;
    QScrollArea *m_scroll;
    QPointF m_pressed;
    bool m_pressValid = false, m_dragged = false;
};
}
#endif
