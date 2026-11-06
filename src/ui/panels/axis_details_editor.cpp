// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
#include "ui/common/numeric_editor.h"
#include "ui/panels/axis_details_editor.h"
#include "ui/common/presentation.h"
#include "ui/panels/style_section.h"
#include "ui/panels/color_button.h"
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>

namespace aster::ui
{
AxisDetailsEditor::AxisDetailsEditor(QWidget *p) : QWidget(p)
{
    auto *layout = styleSections(this);
    StyleSection *f = nullptr;
    auto section = [&](const QString &label) { f = new StyleSection(label); layout->addWidget(f); };
    section(text("ui.style_axis_ticks"));
    auto check = [&](QCheckBox *&b, QString name, QString label)
    {
        b = new QCheckBox(label);
        b->setObjectName(name);
        f->addRow(b);
    };
    check(m_axis, "axisVisible", text("ui.primary_axis_visible"));
    check(m_ticks, "axisTicks", text("ui.major_ticks"));
    check(m_labels, "axisTickLabels", text("ui.tick_labels"));
    check(m_subTicks, "axisSubTicks", text("ui.minor_ticks"));
    m_count = new IntegerEditor;
    m_count->setRange(2, 30);
    m_count->setObjectName("axisTickCount");
    f->addRow(text("ui.target_tick_count"), m_count);
    m_precision = new IntegerEditor;
    m_precision->setRange(0, 15);
    f->addRow(text("ui.number_precision"), m_precision);
    m_format = new ChoiceEditor;
    m_format->addItems({"g", "f", "e", "eb"});
    f->addRow(text("ui.number_format"), m_format);
    m_base = new ChoiceEditor;
    m_base->addItems({"10", "2"});
    f->addRow(text("ui.log_base_heat_map_periods_use_2"), m_base);
    auto stroke = [&](QString label, QDoubleSpinBox *&width, QComboBox *&pen, ColorButton *&c)
    {
        width = new DecimalEditor;
        width->setRange(.25, 6);
        width->setSingleStep(.25);
        f->addRow(label + text("ui.width"), width);
        pen = new ChoiceEditor;
        pen->addItems({text("ui.solid"), text("ui.dash"), text("ui.dot"), text("ui.dash_dot"),
                       text("ui.dash_dot_dot")});
        f->addRow(label + text("ui.pattern"), pen);
        c = new ColorButton;
        f->addRow(label + text("ui.color"), c);
    };
    stroke(text("ui.axis"), m_axisWidth, m_axisPen, m_axisColor);
    section(text("ui.major_grid"));
    stroke(text("ui.major_grid"), m_gridWidth, m_gridPen, m_gridColor);
    section(text("ui.minor_grid"));
    check(m_subGrid, "axisSubGrid", text("ui.minor_grid"));
    stroke(text("ui.minor_grid"), m_subWidth, m_subPen, m_subColor);
    section(text("ui.opposite_axis"));
    check(m_secondary, "axisSecondary", text("ui.opposite_axis_visible"));
    check(m_secondaryTicks, "secondaryTicks", text("ui.opposite_ticks"));
    check(m_secondaryLabels, "secondaryLabels", text("ui.opposite_labels"));
    check(m_independent, "independentOpposite", text("ui.independent_opposite_style_shared_range"));
    m_independent->setToolTip(m_independent->text());
    m_independent->setText(text("ui.independent_opposite_style"));
    stroke(text("ui.opposite_axis"), m_secondaryWidth, m_secondaryPen, m_secondaryColor);
    m_secondaryCount = new IntegerEditor;
    m_secondaryCount->setRange(2, 30);
    m_secondaryPrecision = new IntegerEditor;
    m_secondaryPrecision->setRange(0, 15);
    m_secondaryFormat = new ChoiceEditor;
    m_secondaryFormat->addItems({"g", "f", "e", "eb"});
    f->addRow(text("ui.opposite_tick_count"), m_secondaryCount);
    f->addRow(text("ui.opposite_precision"), m_secondaryPrecision);
    f->addRow(text("ui.opposite_number_format"), m_secondaryFormat);
}
void AxisDetailsEditor::load(const AxisStyle &s)
{
    m_independent->setChecked(s.independentSecondary);
    m_secondaryCount->setValue(s.secondaryCount);
    m_secondaryPrecision->setValue(s.secondaryPrecision);
    m_secondaryWidth->setValue(s.secondaryWidth);
    m_secondaryPen->setCurrentIndex(int(s.secondaryPen) - 1);
    m_secondaryFormat->setCurrentText(s.secondaryFormat);
    m_secondaryColor->setColor(s.secondaryColor);
    m_axis->setChecked(s.axisVisible);
    m_ticks->setChecked(s.ticks);
    m_labels->setChecked(s.tickLabels);
    m_subTicks->setChecked(s.subTicks);
    m_secondary->setChecked(s.secondaryVisible);
    m_secondaryTicks->setChecked(s.secondaryTicks);
    m_secondaryLabels->setChecked(s.secondaryLabels);
    m_subGrid->setChecked(s.subGrid);
    m_count->setValue(s.tickCount);
    m_precision->setValue(s.precision);
    m_format->setCurrentText(s.numberFormat);
    m_base->setCurrentText(QString::number(s.logBase));
    m_axisWidth->setValue(s.axisWidth);
    m_gridWidth->setValue(s.gridWidth);
    m_subWidth->setValue(s.subGridWidth);
    m_axisPen->setCurrentIndex(int(s.axisPen) - 1);
    m_gridPen->setCurrentIndex(int(s.gridPen) - 1);
    m_subPen->setCurrentIndex(int(s.subGridPen) - 1);
    m_axisColor->setColor(s.axisColor);
    m_gridColor->setColor(s.gridColor);
    m_subColor->setColor(s.subGridColor);
}
void AxisDetailsEditor::store(AxisStyle &s) const
{
    s.independentSecondary = m_independent->isChecked();
    s.secondaryCount = m_secondaryCount->value();
    s.secondaryPrecision = m_secondaryPrecision->value();
    s.secondaryWidth = m_secondaryWidth->value();
    s.secondaryPen = Qt::PenStyle(m_secondaryPen->currentIndex() + 1);
    s.secondaryFormat = m_secondaryFormat->currentText();
    s.secondaryColor = m_secondaryColor->color();
    s.axisVisible = m_axis->isChecked();
    s.ticks = m_ticks->isChecked();
    s.tickLabels = m_labels->isChecked();
    s.subTicks = m_subTicks->isChecked();
    s.secondaryVisible = m_secondary->isChecked();
    s.secondaryTicks = m_secondaryTicks->isChecked();
    s.secondaryLabels = m_secondaryLabels->isChecked();
    s.subGrid = m_subGrid->isChecked();
    s.tickCount = m_count->value();
    s.precision = m_precision->value();
    s.numberFormat = m_format->currentText();
    s.logBase = m_base->currentText().toDouble();
    s.axisWidth = m_axisWidth->value();
    s.gridWidth = m_gridWidth->value();
    s.subGridWidth = m_subWidth->value();
    s.axisPen = Qt::PenStyle(m_axisPen->currentIndex() + 1);
    s.gridPen = Qt::PenStyle(m_gridPen->currentIndex() + 1);
    s.subGridPen = Qt::PenStyle(m_subPen->currentIndex() + 1);
    s.axisColor = m_axisColor->color();
    s.gridColor = m_gridColor->color();
    s.subGridColor = m_subColor->color();
}
} // namespace aster::ui
