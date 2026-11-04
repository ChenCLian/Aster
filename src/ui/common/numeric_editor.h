#ifndef ASTER_UI_NUMERIC_EDITOR_H
#define ASTER_UI_NUMERIC_EDITOR_H
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QEvent>
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
        this->lineEdit()->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
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
};
using IntegerEditor = NumericEditor<QSpinBox>;
using DecimalEditor = NumericEditor<QDoubleSpinBox>;
} // namespace aster::ui
#endif
