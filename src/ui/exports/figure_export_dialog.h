// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_FIGURE_EXPORT_DIALOG_H
#define ASTER_FIGURE_EXPORT_DIALOG_H
#include "ui/exports/figure_export.h"
#include <QDialog>
class QSpinBox;
class QComboBox;
class QCheckBox;
namespace aster::ui
{
class FigureExportDialog : public QDialog
{
  public:
    explicit FigureExportDialog(bool multiple, QWidget *parent = nullptr);
    FigureOptions options() const;
    bool wholePage() const;

  private:
    QSpinBox *m_width, *m_height, *m_dpi;
    QComboBox *m_format, *m_scope;
    QCheckBox *m_white;
};
} // namespace aster::ui
#endif
