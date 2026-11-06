// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_COLOR_BUTTON_H
#define ASTER_COLOR_BUTTON_H
#include "ui/common/presentation.h"
#include <QPushButton>
#include <QColorDialog>
namespace aster::ui
{
class ColorButton : public QPushButton
{
    Q_OBJECT
  public:
    explicit ColorButton(QWidget *parent = nullptr) : QPushButton(parent)
    {
        connect(this, &QPushButton::clicked, this,
                [this]
                {
                    auto color = QColorDialog::getColor(
                        m_color, this, aster::ui::text("ui.choose_color"),
                        QColorDialog::ShowAlphaChannel);
                    if (color.isValid())
                        setColor(color);
                });
    }
    void setColor(QColor color)
    {
        const bool changed = color != m_color;
        m_color = color;
        QPixmap swatch(16, 16);
        swatch.fill(color);
        setIcon(QIcon(swatch));
        setText(color.name());
        if (changed) emit colorChanged(color);
    }
    QColor color() const
    {
        return m_color;
    }

  signals:
    void colorChanged(QColor color);
  private:
    QColor m_color{Qt::white};
};
}
#endif
