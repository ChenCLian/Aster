// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_AXIS_DETAILS_EDITOR_H
#define ASTER_AXIS_DETAILS_EDITOR_H
#include "ui/results/plot_style.h"
#include <QWidget>
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;

namespace aster::ui
{
class ColorButton;
class AxisDetailsEditor : public QWidget
{
  public:
    explicit AxisDetailsEditor(QWidget *parent = nullptr);
    void load(const AxisStyle &style);
    void store(AxisStyle &style) const;

  private:
    QCheckBox *m_independent;
    QSpinBox *m_secondaryCount, *m_secondaryPrecision;
    QDoubleSpinBox *m_secondaryWidth;
    QComboBox *m_secondaryPen, *m_secondaryFormat;
    ColorButton *m_secondaryColor;
    QCheckBox *m_axis, *m_ticks, *m_labels, *m_subTicks, *m_secondary, *m_secondaryTicks,
        *m_secondaryLabels, *m_subGrid;
    QSpinBox *m_count, *m_precision;
    QDoubleSpinBox *m_axisWidth, *m_gridWidth, *m_subWidth;
    QComboBox *m_format, *m_base, *m_axisPen, *m_gridPen, *m_subPen;
    ColorButton *m_axisColor, *m_gridColor, *m_subColor;
};
} // namespace aster::ui
#endif
