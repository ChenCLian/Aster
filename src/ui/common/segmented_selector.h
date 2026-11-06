// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_SEGMENTED_SELECTOR_H
#define ASTER_UI_SEGMENTED_SELECTOR_H
#include <QTabBar>
#include <QWheelEvent>

namespace aster::ui
{
class SegmentedSelector : public QTabBar
{
  public:
    explicit SegmentedSelector(QWidget *parent = nullptr) : QTabBar(parent)
    {
        setExpanding(true);
        setDrawBase(false);
        setUsesScrollButtons(false);
        setFocusPolicy(Qt::StrongFocus);
    }

  protected:
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};
} // namespace aster::ui
#endif
