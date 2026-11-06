// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_CHOICE_EDITOR_H
#define ASTER_UI_CHOICE_EDITOR_H
#include <QComboBox>
#include <QAbstractItemView>
#include <QWheelEvent>

namespace aster::ui
{
// Closed selectors never change on wheel input, including when focused.
// The popup keeps its native mouse, keyboard and scrolling behavior.
class ChoiceEditor : public QComboBox
{
  public:
    explicit ChoiceEditor(QWidget *parent = nullptr) : QComboBox(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
    }

  protected:
    void wheelEvent(QWheelEvent *event) override
    {
        if (view()->isVisible())
            QComboBox::wheelEvent(event);
        else
            event->ignore();
    }
};
} // namespace aster::ui
#endif
