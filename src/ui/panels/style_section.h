// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_STYLE_SECTION_H
#define ASTER_STYLE_SECTION_H
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QComboBox>
#include <QResizeEvent>
#include <algorithm>
namespace aster::ui
{
// QScrollArea must reserve the wrapped height, including nested section layouts.
class StyleNote : public QLabel
{
  public:
    explicit StyleNote(const QString &text = {}) : QLabel(text)
    { setWordWrap(true); setMinimumWidth(0); setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum); }
    void setText(const QString &text) { QLabel::setText(text); fitHeight(); }
  protected:
    void resizeEvent(QResizeEvent *event) override { QLabel::resizeEvent(event); fitHeight(); }
  private:
    void fitHeight() { if (width()>0) setMinimumHeight(std::max(0,heightForWidth(width()))); }
};
// Full-width labels avoid narrow QFormLayout columns and remain readable at high DPI.
class StyleSection : public QFrame
{
  public:
    explicit StyleSection(const QString &title, QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName("StyleSection");
        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 2);
        m_layout->setSpacing(8);
        auto *heading = new QLabel(title);
        heading->setObjectName("StyleSectionTitle");
        heading->setWordWrap(true);
        m_layout->addWidget(heading);
    }
    void addRow(QWidget *field)
    {
        if (auto *combo = qobject_cast<QComboBox *>(field))
        {
            combo->setMinimumWidth(0);
            combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumContentsLength(4);
        }
        m_layout->addWidget(field);
    }
    void addRow(const QString &text, QWidget *field)
    {
        auto *label = new QLabel(text);
        label->setObjectName(field->objectName() + "Label");
        label->setWordWrap(true);
        label->setBuddy(field);
        label->setMinimumWidth(0);
        m_layout->addSpacing(3);
        m_layout->addWidget(label);
        addRow(field);
    }
  private:
    QVBoxLayout *m_layout;
};
inline QVBoxLayout *styleSections(QWidget *widget)
{
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 12, 8, 12);
    layout->setSpacing(20);
    layout->setAlignment(Qt::AlignTop);
    return layout;
}
} // namespace aster::ui
#endif
