// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
#include "ui/common/numeric_editor.h"
#include "ui/panels/canvas_details_editor.h"
#include "ui/panels/style_section.h"
#include "ui/panels/color_button.h"
#include "ui/common/presentation.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QFileDialog>
#include <QDoubleSpinBox>
namespace aster::ui
{
CanvasDetailsEditor::CanvasDetailsEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = styleSections(this);
    auto *title = new StyleSection(text("ui.style_title_fonts"));
    layout->addWidget(title);
    m_title = new QLineEdit;
    m_title->setObjectName("plotTitle");
    title->addRow(text("ui.figure_title"), m_title);
    auto font = [&](QDoubleSpinBox *&control, const char *name, const QString &label)
    {
        control = new DecimalEditor;
        control->setObjectName(name);
        control->setRange(6, 32);
        control->setSingleStep(.5);
        control->setSuffix(" pt");
        title->addRow(label, control);
    };
    font(m_titleFont, "plotTitleFontSize", text("ui.title_font_size"));
    font(m_axisFont, "plotAxisFontSize", text("ui.axis_font_size"));
    font(m_legendFont, "plotLegendFontSize", text("ui.legend_font_size"));
    auto *background = new StyleSection(text("ui.background"));
    layout->addWidget(background);
    m_theme = new QCheckBox(text("ui.follow_theme_colors"));
    m_theme->setObjectName("plotFollowTheme");
    m_theme->setToolTip(text("ui.canvas_and_primary_axis_colors_follow_theme"));
    background->addRow(m_theme);
    m_background = new ColorButton;
    m_background->setObjectName("plotBackgroundColor");
    background->addRow(text("ui.background"), m_background);
    m_image = new QLineEdit;
    m_image->setObjectName("plotBackgroundImage");
    background->addRow(text("ui.background_image_clear_path_to_remove"), m_image);
    auto *browse = new QPushButton(text("ui.choose_background_image"));
    background->addRow(browse);
    connect(browse, &QPushButton::clicked, this, [this]
    {
        const auto path = QFileDialog::getOpenFileName(this, text("ui.background_image"), {},
                                                       "Images (*.png *.jpg *.jpeg *.bmp)");
        if (!path.isEmpty()) m_image->setText(path);
    });
    m_aspect = new QCheckBox(text("ui.keep_image_aspect_ratio"));
    background->addRow(m_aspect);
    auto *legend = new StyleSection(text("ui.style_legend_layout"));
    layout->addWidget(legend);
    m_legend = new QCheckBox(text("ui.show_legend"));
    m_legend->setObjectName("plotLegend");
    legend->addRow(m_legend);
    m_position = new ChoiceEditor;
    m_position->setObjectName("legendPosition");
    m_position->addItems({text("ui.top_right"), text("ui.top_left"), text("ui.bottom_right"), text("ui.bottom_left")});
    legend->addRow(text("ui.legend_position"), m_position);
    connect(m_legend, &QCheckBox::toggled, m_position, &QWidget::setEnabled);
    m_swap = new QCheckBox(text("ui.swap_x_y_orientation"));
    m_swap->setObjectName("swapPlotAxes");
    legend->addRow(m_swap);
    auto *scope = new StyleNote(text("ui.changes_apply_to_this_output_of_this_record_and_are_kept_for"));
    scope->setObjectName("Muted");
    scope->setWordWrap(true);
    layout->addWidget(scope);
}
void CanvasDetailsEditor::setFollowingTheme(bool enabled) { m_theme->setChecked(enabled); }
void CanvasDetailsEditor::load(const PlotStyle &s)
{
    m_title->setText(s.title);
    m_background->setColor(s.background);
    m_legend->setChecked(s.legend);
    m_position->setEnabled(s.legend);
    m_position->setCurrentIndex(int(s.legendPosition));
    m_swap->setChecked(s.swapXY);
    m_axisFont->setValue(s.axisFontSize);
    m_legendFont->setValue(s.legendFontSize);
    m_titleFont->setValue(s.titleFontSize);
    m_theme->setChecked(s.followTheme);
    m_image->setText(s.backgroundImage);
    m_aspect->setChecked(s.backgroundKeepAspect);
}
void CanvasDetailsEditor::store(PlotStyle &s) const
{
    s.title = m_title->text();
    s.background = m_background->color();
    s.legend = m_legend->isChecked();
    s.legendPosition = LegendPosition(m_position->currentIndex());
    s.swapXY = m_swap->isChecked();
    s.axisFontSize = m_axisFont->value();
    s.legendFontSize = m_legendFont->value();
    s.titleFontSize = m_titleFont->value();
    s.followTheme = m_theme->isChecked();
    s.backgroundImage = m_image->text().trimmed();
    s.backgroundKeepAspect = m_aspect->isChecked();
}
} // namespace aster::ui
