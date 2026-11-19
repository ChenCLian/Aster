// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_RECORD_ITEM_DELEGATE_H
#define ASTER_RECORD_ITEM_DELEGATE_H
#include "ui/common/file_list_delegate.h"
namespace aster::ui
{
namespace RecordRole
{
constexpr int Id = Qt::UserRole, DisplayName = Id+1, Notes = Id+2,
              FileName = Id+3, StartedAt = Id+4, State = Id+5, RunId = Id+6, WorkflowError = Id+7;
}
class RecordItemDelegate final : public FileListDelegate
{
  public:
    using FileListDelegate::FileListDelegate;
    static FileListStatus statusFor(const QString &state);
    void setOpenedRecord(qint64 id);
    void setChecksVisible(bool visible);
    qint64 openedRecord() const { return m_openedId; }
    bool editorEvent(QEvent *, QAbstractItemModel *, const QStyleOptionViewItem &, const QModelIndex &) override { return false; }

  private:
    Row describe(const QStyleOptionViewItem &, const QModelIndex &index) const override;
    qint64 m_openedId = 0;
    bool m_checksVisible = false;
};
} // namespace aster::ui
#endif
