// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/file_list_delegate.h"
#include "ui/settings/theme_manager.h"
#include "ui/common/presentation.h"
#include <QGuiApplication>
#include <QScreen>
#include <QTextLayout>
#include <QToolTip>
#include <utility>
#include <QFocusEvent>
#include <QFontInfo>
#include <QListView>
#include <QPainter>
#include <QStyleOptionButton>
#include <algorithm>

namespace aster::ui
{
namespace
{
// Break long paths and unbroken filenames at display width. Each line is escaped
// independently so a path or a user note always stays literal text in a tooltip.
QString wrappedField(const QString &value, const QFont &font, int width, int maxLines = 0)
{
    QStringList lines;
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    for (const auto &paragraph : value.split('\n')) {
        QTextLayout layout(paragraph, font);
        layout.setTextOption(option);
        layout.beginLayout();
        for (auto line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            if (maxLines && lines.size() == maxLines) {
                lines.last() += QString(QChar(0x2026));
                layout.endLayout();
                return lines.join("<br>");
            }
            line.setLineWidth(width);
            lines << paragraph.mid(line.textStart(), line.textLength()).toHtmlEscaped();
        }
        layout.endLayout();
    }
    return lines.join("<br>");
}
}

FileListItem::FileListItem(const QString &name, QListWidget *list, Details details, FileListStatus status)
    : QListWidgetItem(name, list), m_details(std::move(details)), m_status(status)
{
}

void FileListItem::setStatus(FileListStatus status)
{
    m_status = status;
}

QVariant FileListItem::data(int role) const
{
    if (role != Qt::ToolTipRole) return QListWidgetItem::data(role);
    const auto name = text();
    const QFont font = QToolTip::font();
    QFont titleFont(font);
    titleFont.setBold(true);
    const auto *screen = listWidget() ? listWidget()->screen() : QGuiApplication::primaryScreen();
    const int width = std::max(100, std::min(380, screen ? screen->availableGeometry().width() - 38 : 380));
    const QString muted = themeColor("muted").name();
    const QString status = aster::ui::text(m_status.label);
    const QFontMetrics metrics(font);
    const int statusWidth = metrics.horizontalAdvance(status) + 12;
    int cardWidth = std::max(190, QFontMetrics(titleFont).horizontalAdvance(name) + statusWidth + 12);
    for (const auto &line : m_details.source.split('\n'))
        cardWidth = std::max(cardWidth, metrics.horizontalAdvance(line));
    cardWidth = std::clamp(std::max(cardWidth, metrics.horizontalAdvance(m_details.record)), 1, width);
    const int titleWidth = std::max(30, cardWidth - statusWidth);
    QString html = QString("<table width='%1' cellspacing='0' cellpadding='0'><tr>"
                           "<td width='%2' valign='top'><b>%3</b></td>"
                           "<td align='right' valign='top' style='color:%4'>%5</td></tr></table>")
        .arg(cardWidth).arg(titleWidth).arg(wrappedField(name, titleFont, titleWidth),
                                            themeColor(m_status.color).name(), status.toHtmlEscaped());
    if (!m_details.source.isEmpty() && m_details.source != name)
        html += "<p style='margin:7px 0 0;color:" + muted + "'>" + wrappedField(m_details.source, font, width) + "</p>";
    if (!m_details.record.isEmpty())
        html += "<p style='margin:3px 0 0;color:" + muted + "'>" + m_details.record.toHtmlEscaped() + "</p>";
    if (!m_details.notes.isEmpty())
        html += "<p style='margin:6px 0 0'>" + wrappedField(aster::ui::text("ui.notes") + QStringLiteral(" · ") + m_details.notes, font, width, 3) + "</p>";
    if (m_details.workflowError)
        html += "<p style='margin:6px 0 0'>" + aster::ui::text("ui.record_workflow_error").toHtmlEscaped() + "</p>";
    return html;
}

FileListDelegate::FileListDelegate(QListView *view) : QStyledItemDelegate(view), m_view(view)
{
    view->installEventFilter(this);
    view->viewport()->installEventFilter(this);
}

bool FileListDelegate::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_view && watched != m_view->viewport())
        return QStyledItemDelegate::eventFilter(watched, event);
    bool keyboard = m_keyboardFocus;
    switch (event->type()) {
    case QEvent::KeyPress:
        keyboard = true;
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        keyboard = false;
        break;
    case QEvent::FocusIn: {
        const auto reason = static_cast<QFocusEvent *>(event)->reason();
        if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
            reason == Qt::ShortcutFocusReason) keyboard = true;
        else if (reason == Qt::MouseFocusReason) keyboard = false;
        break;
    }
    default: return false;
    }
    if (m_keyboardFocus != keyboard) {
        m_keyboardFocus = keyboard;
        m_view->viewport()->update(m_view->visualRect(m_view->currentIndex()));
    }
    return false;
}

QSize FileListDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    auto opt = option;
    initStyleOption(&opt, index);
    return {100, std::max(32, opt.fontMetrics.height() + 12)};
}

QRect FileListDelegate::checkRect(const QRect &row)
{
    return {row.left() + 7, row.center().y() - 8, 18, 18};
}

void FileListDelegate::paint(QPainter *p, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    auto opt = option;
    initStyleOption(&opt, index);
    const Row row = describe(opt, index);
    const QRect rect = opt.rect;
    const bool hovered = opt.state.testFlag(QStyle::State_MouseOver);
    const char *fill = "navigation";
    if (row.highlighted) fill = "fileListSelection";
    else if (hovered) fill = "fileListHover";

    p->save();
    p->setClipRect(rect, Qt::IntersectClip);
    p->fillRect(rect, themeColor(fill));
    p->setRenderHint(QPainter::Antialiasing);
    const qreal centerY = QRectF(rect).center().y();
    if (row.current) {
        p->setPen(Qt::NoPen);
        p->setBrush(themeColor("fileListMark"));
        p->drawRoundedRect(QRectF(rect.left() + 2, centerY - 8, 3, 16), 1.5, 1.5);
    }

    const bool checkable = opt.features.testFlag(QStyleOptionViewItem::HasCheckIndicator);
    const QRect slot = checkRect(rect);
    if (checkable && row.showCheckbox) {
        QStyleOptionButton check;
        check.rect = slot;
        check.palette = opt.palette;
        check.state = opt.state & QStyle::State_Enabled;
        check.state |= opt.checkState == Qt::Checked ? QStyle::State_On : QStyle::State_Off;
        m_view->style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &check, p, m_view);
    } else if (!row.workflowError) {
        const QColor dot = themeColor(row.dotColor);
        p->setPen(row.hollowDot ? QPen(dot, 1.4) : QPen(Qt::NoPen));
        p->setBrush(row.hollowDot ? QBrush(Qt::NoBrush) : QBrush(dot));
        const qreal dotX = checkable ? QRectF(slot).center().x() : rect.left() + 14;
        p->drawEllipse(QPointF(dotX, centerY), 3.5, 3.5);
    }
    QRect nameRect = rect.adjusted(0, 0, -8, 0);
    nameRect.setLeft(checkable ? slot.right() + 7 : rect.left() + 27);
    if (row.workflowError) {
        QRect marker = nameRect;
        marker.setLeft(marker.right() - std::max(16, opt.fontMetrics.height()) + 1);
        auto font = opt.font;
        font.setPointSizeF(QFontInfo(opt.font).pointSizeF() + 1);
        font.setBold(true);
        p->setFont(font);
        p->setPen(themeColor("error"));
        p->drawText(marker, Qt::AlignCenter, QStringLiteral("!"));
        nameRect.setRight(marker.left() - 4);
    }
    p->setFont(opt.font);
    p->setPen(themeColor("text"));
    p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                opt.fontMetrics.elidedText(row.name, Qt::ElideMiddle, nameRect.width()));

    if (m_keyboardFocus && opt.state.testFlag(QStyle::State_HasFocus)) {
        p->setPen(QPen(themeColor("fileListMark"), 1, Qt::DotLine));
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(rect).adjusted(1.5, 1.5, -1.5, -1.5));
    }
    p->restore();
}
} // namespace aster::ui
