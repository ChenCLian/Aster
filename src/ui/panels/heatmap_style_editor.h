#ifndef ASTER_HEATMAP_STYLE_EDITOR_H
#define ASTER_HEATMAP_STYLE_EDITOR_H
#include "ui/results/plot_style.h"
#include <QWidget>
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
namespace aster::ui
{
class ColorButton;
class StyleNote;
class HeatmapStyleEditor : public QWidget
{
  public:
    explicit HeatmapStyleEditor(QWidget *parent = nullptr);
    void load(const PlotStyle &style, const QString &status, bool significanceAvailable);
    void store(PlotStyle &style) const;
    void setStatus(const QString &status, bool available);
    void setAutomaticRange(double lower, double upper);
  private:
    QComboBox *m_gradient, *m_mapping, *m_pixels;
    QCheckBox *m_reverse, *m_bar, *m_manual, *m_contour;
    QSpinBox *m_levels;
    QLineEdit *m_lower, *m_upper;
    ColorButton *m_contourColor;
    QDoubleSpinBox *m_contourWidth;
    StyleNote *m_status;
};
} // namespace aster::ui
#endif
