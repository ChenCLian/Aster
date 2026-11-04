#include "ui/common/numeric_editor.h"
#include "ui/panels/heatmap_style_editor.h"
#include "ui/panels/style_section.h"
#include "ui/panels/color_button.h"
#include "ui/common/presentation.h"
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QLineEdit>
#include <QLocale>
#include <limits>
namespace aster::ui
{
HeatmapStyleEditor::HeatmapStyleEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = styleSections(this);
    auto *palette = new StyleSection(text("ui.heatmap_palette"));
    layout->addWidget(palette);
    m_gradient = new QComboBox;
    m_gradient->setObjectName("heatmapGradient");
    m_gradient->addItems({text("ui.scientific"), text("ui.grayscale"), text("ui.thermal"), text("ui.diverging"), text("ui.spectrum")});
    palette->addRow(text("ui.heat_map_gradient"), m_gradient);
    m_reverse = new QCheckBox(text("ui.heatmap_reverse"));
    m_reverse->setObjectName("heatmapReverse");
    palette->addRow(m_reverse);
    m_levels = new IntegerEditor;
    m_levels->setObjectName("heatmapLevels");
    m_levels->setRange(2, 1024);
    palette->addRow(text("ui.heatmap_levels"), m_levels);
    m_bar = new QCheckBox(text("ui.heatmap_colorbar"));
    m_bar->setObjectName("heatmapColorbar");
    palette->addRow(m_bar);
    auto *mapping = new StyleSection(text("ui.heatmap_mapping"));
    layout->addWidget(mapping);
    m_mapping = new QComboBox;
    m_mapping->setObjectName("heatmapMapping");
    m_mapping->addItems({text("ui.heatmap_linear"), text("ui.heatmap_log")});
    mapping->addRow(text("ui.heatmap_intensity"), m_mapping);
    auto *note = new StyleNote(text("ui.heatmap_mapping_note"));
    note->setObjectName("Muted"); note->setWordWrap(true);
    mapping->addRow(note);
    m_manual = new QCheckBox(text("ui.fixed_color_range"));
    m_manual->setObjectName("heatmapManual");
    mapping->addRow(m_manual);
    auto number = [](const char *name)
    {
        auto *edit = new QLineEdit;
        edit->setObjectName(name);
        auto *validator = new QDoubleValidator(edit);
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::ScientificNotation);
        edit->setValidator(validator);
        return edit;
    };
    m_lower = number("heatmapLower"); m_upper = number("heatmapUpper");
    mapping->addRow(text("ui.color_minimum"), m_lower);
    mapping->addRow(text("ui.color_maximum"), m_upper);
    connect(m_manual, &QCheckBox::toggled, m_lower, &QWidget::setEnabled);
    connect(m_manual, &QCheckBox::toggled, m_upper, &QWidget::setEnabled);
    auto *pixels = new StyleSection(text("ui.heatmap_pixels"));
    layout->addWidget(pixels);
    m_pixels = new QComboBox;
    m_pixels->setObjectName("heatmapPixels");
    m_pixels->addItems({text("ui.heatmap_nearest"), text("ui.heatmap_smooth")});
    pixels->addRow(text("ui.heatmap_pixel_mode"), m_pixels);
    auto *contours = new StyleSection(text("ui.heatmap_significance"));
    layout->addWidget(contours);
    m_contour = new QCheckBox(text("ui.heatmap_contour"));
    m_contour->setObjectName("heatmapContour");
    contours->addRow(m_contour);
    m_contourColor = new ColorButton;
    m_contourColor->setObjectName("heatmapContourColor");
    contours->addRow(text("ui.color_1d0c83"), m_contourColor);
    m_contourWidth = new DecimalEditor;
    m_contourWidth->setObjectName("heatmapContourWidth");
    m_contourWidth->setRange(.25, 6); m_contourWidth->setSingleStep(.25);
    contours->addRow(text("ui.line_width"), m_contourWidth);
    connect(m_contour, &QCheckBox::toggled, this, [this](bool on)
    { m_contourColor->setEnabled(on); m_contourWidth->setEnabled(on); });
    m_status = new StyleNote;
    m_status->setObjectName("heatmapStatus"); m_status->setWordWrap(true);
    contours->addRow(m_status);
    auto *meaning = new StyleNote(text("ui.heatmap_contour_note"));
    meaning->setObjectName("Muted"); meaning->setWordWrap(true);
    contours->addRow(meaning);
}
void HeatmapStyleEditor::load(const PlotStyle &s, const QString &status, bool available)
{
    m_gradient->setCurrentIndex(s.colorGradient);
    m_reverse->setChecked(s.colorReverse); m_levels->setValue(s.colorLevels);
    m_bar->setChecked(s.colorScaleVisible);
    m_mapping->setCurrentIndex(s.colorLog ? 1 : 0);
    m_pixels->setCurrentIndex(s.colorInterpolate ? 1 : 0);
    m_manual->setChecked(s.colorManual);
    m_lower->setText(QString::number(s.colorLower, 'g', 15));
    m_upper->setText(QString::number(s.colorUpper, 'g', 15));
    m_lower->setEnabled(s.colorManual); m_upper->setEnabled(s.colorManual);
    m_contour->setChecked(s.significanceContour); m_contour->setEnabled(available);
    m_contourColor->setColor(s.significanceColor); m_contourWidth->setValue(s.significanceWidth);
    m_contourColor->setEnabled(available && s.significanceContour);
    m_contourWidth->setEnabled(available && s.significanceContour);
    setStatus(status, available);
}
void HeatmapStyleEditor::setStatus(const QString &status, bool available)
{
    m_status->setText(status); m_contour->setEnabled(available);
    m_contourColor->setEnabled(available && m_contour->isChecked());
    m_contourWidth->setEnabled(available && m_contour->isChecked());
}
void HeatmapStyleEditor::setAutomaticRange(double lower, double upper)
{
    if (m_manual->isChecked()) return;
    m_lower->setText(QString::number(lower,'g',15)); m_upper->setText(QString::number(upper,'g',15));
}
void HeatmapStyleEditor::store(PlotStyle &s) const
{
    s.colorGradient = m_gradient->currentIndex(); s.colorReverse = m_reverse->isChecked();
    s.colorLevels = m_levels->value(); s.colorScaleVisible = m_bar->isChecked();
    s.colorLog = m_mapping->currentIndex() == 1; s.colorInterpolate = m_pixels->currentIndex() == 1;
    s.colorManual = m_manual->isChecked();
    auto number = [](QLineEdit *e) { bool ok = false; auto v = QLocale::c().toDouble(e->text(), &ok);
        return ok ? v : std::numeric_limits<double>::quiet_NaN(); };
    s.colorLower = number(m_lower); s.colorUpper = number(m_upper);
    s.significanceContour = m_contour->isChecked(); s.significanceColor = m_contourColor->color();
    s.significanceWidth = m_contourWidth->value();
}
} // namespace aster::ui
