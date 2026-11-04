#ifndef ASTER_CONTOUR_PLOTTABLE_H
#define ASTER_CONTOUR_PLOTTABLE_H
#include "third_party/qcustomplot/qcustomplot.h"
namespace aster::ui
{
class ContourPlottable : public QCPAbstractPlottable
{
  public:
    ContourPlottable(QCPAxis *x, QCPAxis *y, QVector<QLineF> segments);
    const QVector<QLineF> &segments() const { return m_segments; }
    int segmentCount() const { return int(m_segments.size()); }
    double selectTest(const QPointF &, bool, QVariant * = nullptr) const override { return -1; }
    QCPRange getKeyRange(bool &found, QCP::SignDomain = QCP::sdBoth) const override;
    QCPRange getValueRange(bool &found, QCP::SignDomain = QCP::sdBoth, const QCPRange & = {}) const override;
  protected:
    void draw(QCPPainter *painter) override;
    void drawLegendIcon(QCPPainter *painter, const QRectF &rect) const override;
  private:
    QVector<QLineF> m_segments;
};
} // namespace aster::ui
#endif
