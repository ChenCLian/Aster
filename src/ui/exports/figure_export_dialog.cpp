// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
#include "ui/common/numeric_editor.h"
#include "ui/exports/figure_export_dialog.h"
#include "ui/common/presentation.h"
#include <QFormLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
namespace aster::ui
{
FigureExportDialog::FigureExportDialog(bool multiple, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(text("ui.export_figure"));
    resize(480, 430);
    auto *f = new QFormLayout(this);
    f->setRowWrapPolicy(QFormLayout::WrapAllRows);
    m_scope = new ChoiceEditor;
    m_scope->addItem(text("ui.selected_subplot"));
    if (multiple)
        m_scope->addItem(text("ui.whole_current_page"));
    f->addRow(text("ui.export_scope"), m_scope);
    m_format = new ChoiceEditor;
    m_format->addItems({"PNG", "PDF"});
    f->addRow(text("ui.format"), m_format);
    m_width = new IntegerEditor;
    m_width->setRange(320, PlotLimits::maxDimension);
    m_width->setValue(2400);
    m_height = new IntegerEditor;
    m_height->setRange(240, PlotLimits::maxDimension);
    m_height->setValue(1600);
    m_dpi = new IntegerEditor;
    m_dpi->setRange(72, 600);
    m_dpi->setValue(200);
    f->addRow(text("ui.width_pixels"), m_width);
    f->addRow(text("ui.height_pixels"), m_height);
    f->addRow("DPI", m_dpi);
    m_white = new QCheckBox(text("ui.use_white_paper_background"));
    m_white->setChecked(true);
    f->addRow(m_white);
    auto *note =
        new QLabel(text("ui.exports_the_current_view_pdf_curves_are_vector_heat_maps_are"));
    note->setWordWrap(true);
    note->setObjectName("Muted");
    f->addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    f->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this,
            [this]
            {
                auto o = options();
                o.path = "validation";
                auto error = validateFigureOptions(o);
                if (error.isEmpty())
                    accept();
                else
                    QMessageBox::warning(this, windowTitle(), error);
            });
}
FigureOptions FigureExportDialog::options() const
{
    FigureOptions o;
    o.format =
        m_format->currentIndex() == 0 ? FigureOptions::Format::Png : FigureOptions::Format::Pdf;
    o.pixels = {m_width->value(), m_height->value()};
    o.dpi = m_dpi->value();
    o.whitePaper = m_white->isChecked();
    return o;
}
bool FigureExportDialog::wholePage() const
{
    return m_scope->currentIndex() == 1;
}
} // namespace aster::ui
