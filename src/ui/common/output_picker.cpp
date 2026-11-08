// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/output_picker.h"
#include "ui/common/popup_placement.h"
#include "ui/common/presentation.h"
#include <QApplication>
#include <QCloseEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>
namespace aster::ui
{
OutputPicker::OutputPicker(QWidget *owner, QString title, QString destination,
                           const QVector<OutputChoice> &choices)
    : QFrame(owner, Qt::Popup | Qt::FramelessWindowHint),
      m_owner(owner ? owner->window() : nullptr), m_previousFocus(QApplication::focusWidget())
{
    setObjectName("outputPicker");
    setAttribute(Qt::WA_DeleteOnClose);
    auto bodyFont = QApplication::font();
    bodyFont.setPointSizeF(std::max(11.0, bodyFont.pointSizeF()));
    setFont(bodyFont);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);
    auto *heading = new QLabel(std::move(title));
    setAccessibleName(heading->text());
    heading->setObjectName("outputPickerTitle");
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    heading->setMinimumWidth(0);
    layout->addWidget(heading);
    auto *context = new QLabel(std::move(destination));
    context->setObjectName("outputPickerContext");
    context->setTextFormat(Qt::PlainText);
    context->setWordWrap(true);
    context->setMinimumWidth(0);
    layout->addWidget(context);
    m_search = new QLineEdit;
    m_search->setObjectName("outputSearch");
    m_search->setPlaceholderText(text("ui.search_outputs"));
    m_search->setAccessibleName(m_search->placeholderText());
    m_search->setClearButtonEnabled(true);
    m_search->setVisible(choices.size() > 8);
    m_search->installEventFilter(this);
    layout->addWidget(m_search);
    m_list = new QListWidget;
    m_list->setObjectName("outputChoices");
    m_list->setAccessibleName(heading->text());
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setMinimumSize(0, 0);
    m_list->installEventFilter(this);
    layout->addWidget(m_list, 1);
    auto preferredFont = bodyFont;
    preferredFont.setBold(true);
    m_rowHeight = std::max(36, QFontMetrics(preferredFont).height() + 16);
    int initial = 0;
    for (const auto &choice : choices)
    {
        auto *item = new QListWidgetItem(choice.label, m_list);
        item->setData(Qt::UserRole, choice.key);
        item->setFont(choice.preferred ? preferredFont : bodyFont);
        item->setToolTip(choice.label + (choice.preferred ? "\n" + text("ui.default_output") : QString()));
        item->setSizeHint(QSize(0, m_rowHeight));
        if (choice.preferred) initial = m_list->count() - 1;
    }
    m_list->setCurrentRow(initial);
    m_empty = new QLabel(text("ui.no_matching_outputs"));
    m_empty->setObjectName("outputPickerEmpty");
    m_empty->setWordWrap(true);
    m_empty->hide();
    layout->addWidget(m_empty);
    connect(m_list, &QListWidget::itemClicked, this, &OutputPicker::choose);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &query) {
        QListWidgetItem *first = nullptr;
        for (int row = 0; row < m_list->count(); ++row)
        {
            auto *item = m_list->item(row);
            const bool match = item->text().contains(query.trimmed(), Qt::CaseInsensitive);
            item->setHidden(!match);
            if (match && !first) first = item;
        }
        if (!m_list->currentItem() || m_list->currentItem()->isHidden()) m_list->setCurrentItem(first);
        m_empty->setVisible(!first);
    });
    if (m_owner) m_owner->installEventFilter(this);
}
void OutputPicker::openAt(const QRect &anchor)
{
    if (m_dismissed) return;
    if (m_owner && !m_owner->isVisible()) { close(); return; }
    ensurePolished();
    const auto bounds = popupBounds(m_owner, anchor);
    const int width = std::min(480, bounds.width());
    int textWidth = 0;
    for (int i = 0; i < m_list->count(); ++i)
    {
        const auto *item = m_list->item(i);
        textWidth = std::max(textWidth, QFontMetrics(item->font()).horizontalAdvance(item->text()));
    }
    const int desiredWidth = std::min(width, std::max(360, textWidth + 52));
    const int rows = std::clamp(m_list->count(), 1, 8);
    // Ask the layout for wrapped headings at the actual width; no label can force
    // this popup beyond the owner's visible area.
    layout()->setSizeConstraint(QLayout::SetNoConstraint);
    resize(desiredWidth, 200);
    layout()->activate();
    int headerHeight = 26;
    for (auto *label : findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly))
        if (label != m_empty)
            headerHeight += std::max(label->fontMetrics().height(), label->heightForWidth(desiredWidth - 20)) + 6;
    if (!m_search->isHidden()) headerHeight += m_search->sizeHint().height() + 6;
    const auto geometry = popupGeometry(QSize(desiredWidth, headerHeight + rows * m_rowHeight + 4), anchor, bounds);
    setGeometry(geometry);
    show();
    // Native styles may adjust popup geometry during show; enforce the same logical bounds.
    setGeometry(geometry);
    m_list->setFocus(Qt::PopupFocusReason);
    m_list->scrollToItem(m_list->currentItem());
}
void OutputPicker::choose(QListWidgetItem *item)
{
    if (m_committed || !item || item->isHidden()) return;
    const auto key = item->data(Qt::UserRole).toString();
    if (key.isEmpty()) return;
    m_committed = true;
    close();
    emit chosen(key);
}
bool OutputPicker::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_owner &&
        (event->type() == QEvent::Hide || event->type() == QEvent::Close ||
         (isVisible() && (event->type() == QEvent::Move || event->type() == QEvent::Resize))))
        close();
    if ((watched == m_list || watched == m_search) && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) { close(); return true; }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
        { choose(m_list->currentItem()); return true; }
        if (watched == m_search && key->key() == Qt::Key_Down)
        { m_list->setFocus(); return true; }
        if (watched == m_list && key->matches(QKeySequence::Find) && m_search->isVisible())
        { m_search->setFocus(); m_search->selectAll(); return true; }
    }
    return QFrame::eventFilter(watched, event);
}
void OutputPicker::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) { close(); event->accept(); return; }
    QFrame::keyPressEvent(event);
}
void OutputPicker::hideEvent(QHideEvent *event)
{
    QFrame::hideEvent(event);
    if (m_previousFocus && m_previousFocus->isVisible() && m_previousFocus->isEnabled())
        m_previousFocus->setFocus(Qt::PopupFocusReason);
}
void OutputPicker::closeEvent(QCloseEvent *event)
{
    m_dismissed = true;
    QFrame::closeEvent(event);
}
} // namespace aster::ui
