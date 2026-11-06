// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_SETTINGS_DIALOG_H
#define ASTER_SETTINGS_DIALOG_H
#include "ui/common/app_settings.h"
#include <QDialog>
namespace aster::ui
{
class SettingsDialog : public QDialog
{
  public:
    explicit SettingsDialog(AppSettings settings, QWidget *parent = nullptr);
    const AppSettings &values() const
    {
        return m_values;
    }

  private:
    AppSettings m_values;
};
} // namespace aster::ui
#endif
