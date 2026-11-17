// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_WRAPPING_BUTTON_H
#define ASTER_WRAPPING_BUTTON_H
#include <QCheckBox>
#include <QPushButton>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QStyleOptionFocusRect>
#include <QTextBoundaryFinder>
#include <QResizeEvent>
#include <QShowEvent>
#include <algorithm>
#include <type_traits>

namespace aster::ui
{
// Text-only controls for narrow inspectors. Keep QAbstractButton's real text,
// accessibility, signals and keyboard behavior; only its layout/painting wraps.
template <class Base> class WrappingButton : public Base
{
  public:
    explicit WrappingButton(const QString &text, QWidget *parent = nullptr)
        : Base(text, parent)
    {
        auto policy = this->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Preferred);
        policy.setVerticalPolicy(QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        this->setSizePolicy(policy);
    }
    bool hasHeightForWidth() const override { return true; }
    QSize sizeHint() const override
    {
        const auto natural = Base::sizeHint();
        return {natural.width(), heightForWidth(natural.width())};
    }
    QSize minimumSizeHint() const override
    {
        const auto natural = Base::sizeHint();
        const auto content = textRect(QRect(QPoint(), natural));
        // Preserve complete words (and Unicode line-break opportunities) rather
        // than claiming a zero minimum and silently clipping a long label.
        int wordWidth = 0;
        qsizetype start = 0;
        QTextBoundaryFinder words(QTextBoundaryFinder::Word, this->text());
        for (qsizetype end = words.toNextBoundary(); end >= 0; end = words.toNextBoundary()) {
            wordWidth = std::max(wordWidth, this->fontMetrics().horizontalAdvance(
                this->text().mid(start, end - start).trimmed()));
            start = end;
        }
        return {std::min(natural.width(), wordWidth + natural.width() - content.width()),
                heightForWidth(natural.width())};
    }
    int heightForWidth(int width) const override
    {
        const auto natural = Base::sizeHint();
        const auto content = textRect(QRect(0, 0, width, natural.height()));
        const auto bounds = this->fontMetrics().boundingRect(
            QRect(0, 0, std::max(1, content.width()), 100000), textFlags(), this->text());
        return std::max(natural.height(), bounds.height() + natural.height() - content.height());
    }
  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        Base::resizeEvent(event);
        fitHeight();
    }
    void showEvent(QShowEvent *event) override
    {
        Base::showEvent(event);
        fitHeight();
    }
    void changeEvent(QEvent *event) override
    {
        Base::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            fitHeight();
    }
    bool hitButton(const QPoint &point) const override { return this->rect().contains(point); }
    void paintEvent(QPaintEvent *) override
    {
        QStylePainter painter(this);
        QStyleOptionButton option;
        this->initStyleOption(&option);
        auto content = textRect(option.rect);
        option.text.clear();
        painter.drawControl(isCheck ? QStyle::CE_CheckBox : QStyle::CE_PushButton, option);
        if (!isCheck && (option.state & QStyle::State_Sunken))
            content.translate(this->style()->pixelMetric(QStyle::PM_ButtonShiftHorizontal, &option, this),
                              this->style()->pixelMetric(QStyle::PM_ButtonShiftVertical, &option, this));
        this->style()->drawItemText(&painter, content, textFlags(), option.palette,
                                   this->isEnabled(), this->text(), QPalette::ButtonText);
        if (isCheck && this->hasFocus()) {
            QStyleOptionFocusRect focus;
            focus.initFrom(this);
            focus.rect = content;
            painter.drawPrimitive(QStyle::PE_FrameFocusRect, focus);
        }
    }
  private:
    void fitHeight()
    {
        // heightForWidth is a preferred height; nested, constrained layouts can
        // shrink it to the native one-line minimum. Reserve the full painted
        // text height and release it again when the column widens/font shrinks.
        const int required = heightForWidth(this->width());
        if (this->minimumHeight() != required) this->setMinimumHeight(required);
    }
    static constexpr bool isCheck = std::is_same_v<Base, QCheckBox>;
    int textFlags() const
    {
        return Qt::TextWordWrap | Qt::AlignVCenter | (isCheck ? Qt::AlignLeft : Qt::AlignHCenter) |
               (this->style()->styleHint(QStyle::SH_UnderlineShortcut, nullptr, this)
                    ? Qt::TextShowMnemonic : Qt::TextHideMnemonic);
    }
    QRect textRect(const QRect &rect) const
    {
        QStyleOptionButton option;
        this->initStyleOption(&option);
        option.rect = rect;
        return this->style()->subElementRect(
            isCheck ? QStyle::SE_CheckBoxContents : QStyle::SE_PushButtonContents, &option, this);
    }
};
using WrappingCheckBox = WrappingButton<QCheckBox>;
using WrappingPushButton = WrappingButton<QPushButton>;
}
#endif
