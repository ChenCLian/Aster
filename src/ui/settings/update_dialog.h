// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UPDATE_DIALOG_H
#define ASTER_UPDATE_DIALOG_H
#include "update/update_checker.h"
#include <QDialog>
namespace aster::ui {
class UpdateDialog : public QDialog {
public:
    explicit UpdateDialog(QWidget *parent=nullptr);
};
}
#endif
