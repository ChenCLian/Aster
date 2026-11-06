// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_SECTION_SPLITTER_H
#define ASTER_SECTION_SPLITTER_H

#include "ui/settings/theme_manager.h"
#include <QEnterEvent>
#include <QPainter>
#include <QSplitter>

namespace aster::ui
{
// A thin separator with a full-size drag target between sections of one panel.
class SectionSplitter : public QSplitter
{
  public:
    explicit SectionSplitter(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSplitter(orientation, parent)
    {
        setHandleWidth(9);
    }

  protected:
    QSplitterHandle *createHandle() override
    {
        class Handle final : public QSplitterHandle
        {
          public:
            Handle(Qt::Orientation orientation, QSplitter *parent)
                : QSplitterHandle(orientation, parent)
            {
                connect(&ThemeManager::instance(), &ThemeManager::changed,
                        this, qOverload<>(&QWidget::update));
            }
          protected:
            void paintEvent(QPaintEvent *) override
            {
                QPainter painter(this);
                const int thickness = underMouse() ? 2 : 1;
                const auto color = themeColor(underMouse() ? "controlBorder" : "divider");
                if (orientation() == Qt::Vertical)
                    painter.fillRect(0, (height() - thickness) / 2, width(), thickness, color);
                else
                    painter.fillRect((width() - thickness) / 2, 0, thickness, height(), color);
            }
            void enterEvent(QEnterEvent *event) override
            { QSplitterHandle::enterEvent(event); update(); }
            void leaveEvent(QEvent *event) override
            { QSplitterHandle::leaveEvent(event); update(); }
        };
        return new Handle(orientation(), this);
    }
};
} // namespace aster::ui
#endif
