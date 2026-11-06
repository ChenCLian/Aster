// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_CANVAS_DETAILS_EDITOR_H
#define ASTER_CANVAS_DETAILS_EDITOR_H
#include "ui/results/plot_style.h"
#include <QWidget>
class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QComboBox;
namespace aster::ui
{
class ColorButton;
class CanvasDetailsEditor : public QWidget
{
    Q_OBJECT
  public:
    explicit CanvasDetailsEditor(QWidget *parent = nullptr);
    void load(const PlotStyle &style);
    void store(PlotStyle &style) const;
    void setFollowingTheme(bool enabled);
  private:
    QCheckBox *m_theme, *m_aspect, *m_legend, *m_swap;
    QLineEdit *m_image, *m_title;
    QComboBox *m_position;
    ColorButton *m_background;
    QDoubleSpinBox *m_axisFont, *m_legendFont, *m_titleFont;
};
} // namespace aster::ui
#endif
