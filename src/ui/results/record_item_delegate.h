// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_RECORD_ITEM_DELEGATE_H
#define ASTER_RECORD_ITEM_DELEGATE_H
#include <QStyledItemDelegate>
namespace aster::ui
{
namespace RecordRole
{
constexpr int Id = Qt::UserRole, DisplayName = Id+1, Notes = Id+2,
              FileName = Id+3, StartedAt = Id+4, State = Id+5, RunId = Id+6;
}
class RecordItemDelegate : public QStyledItemDelegate
{
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static QRect checkRect(const QRect &row) { return QRect(row.left()+12,row.center().y()-9,18,18); }
    bool editorEvent(QEvent *, QAbstractItemModel *, const QStyleOptionViewItem &, const QModelIndex &) override { return false; }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
} // namespace aster::ui
#endif
