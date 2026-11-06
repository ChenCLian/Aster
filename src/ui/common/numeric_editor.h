// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_NUMERIC_EDITOR_H
#define ASTER_UI_NUMERIC_EDITOR_H
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QEvent>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>

namespace aster::ui
{
// Keep a minimum text inset even when a platform style positions the embedded
// editor at the frame edge. Do not add the inset twice when QSS already supplies it.
template <class Base> class NumericEditor : public Base
{
  public:
    explicit NumericEditor(QWidget *parent = nullptr) : Base(parent)
    {
        this->setFocusPolicy(Qt::StrongFocus);
        this->lineEdit()->installEventFilter(this);
    }

  protected:
    void focusInEvent(QFocusEvent *event) override
    {
        m_wheelEditing = event->reason() == Qt::MouseFocusReason ||
                         event->reason() == Qt::TabFocusReason ||
                         event->reason() == Qt::BacktabFocusReason;
        Base::focusInEvent(event);
    }
    void focusOutEvent(QFocusEvent *event) override
    {
        m_wheelEditing = false;
        Base::focusOutEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_wheelEditing = true;
        Base::mousePressEvent(event);
    }
    void wheelEvent(QWheelEvent *event) override
    {
        if (this->hasFocus() && m_wheelEditing)
            Base::wheelEvent(event);
        else
            event->ignore(); // Let the enclosing parameter panel scroll.
    }
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == this->lineEdit() && event->type() == QEvent::MouseButtonPress &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
            m_wheelEditing = true;
        const bool handled = Base::eventFilter(object, event);
        if (object == this->lineEdit() &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
             event->type() == QEvent::Show || event->type() == QEvent::StyleChange))
        {
            auto *edit = this->lineEdit();
            auto margins = edit->textMargins();
            // QLineEdit supplies two additional pixels inside its text area.
            margins.setLeft(std::max(0, 8 - edit->geometry().left() - 2));
            if (margins != edit->textMargins())
                edit->setTextMargins(margins);
        }
        return handled;
    }

  private:
    bool m_wheelEditing = false;
};
using IntegerEditor = NumericEditor<QSpinBox>;
using DecimalEditor = NumericEditor<QDoubleSpinBox>;
} // namespace aster::ui
#endif
