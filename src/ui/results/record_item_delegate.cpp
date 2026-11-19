// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/results/record_item_delegate.h"
#include <QListView>

namespace aster::ui
{
FileListStatus RecordItemDelegate::statusFor(const QString &state)
{
    if (state == "Succeeded") return {"ui.completed", "success"};
    if (state == "PartialFailed") return {"ui.record_partial_failure", "error"};
    if (state == "Failed") return {"ui.failed", "error"};
    if (state == "Cancelled") return {"ui.cancelled"};
    if (state == "Invalid") return {"ui.record_invalid"};
    return {"ui.record_unknown"};
}

void RecordItemDelegate::setOpenedRecord(qint64 id)
{
    if (m_openedId == id)
        return;
    m_openedId = id;
    m_view->viewport()->update();
}

void RecordItemDelegate::setChecksVisible(bool visible)
{
    if (m_checksVisible == visible) return;
    m_checksVisible = visible;
    m_view->viewport()->update();
}

FileListDelegate::Row RecordItemDelegate::describe(const QStyleOptionViewItem &,
                                                   const QModelIndex &index) const
{
    Row row;
    row.name = index.data(RecordRole::FileName).toString();
    row.current = m_openedId != 0 && index.data(RecordRole::Id).toLongLong() == m_openedId;
    row.highlighted = row.current;
    row.showCheckbox = m_checksVisible;
    row.workflowError = index.data(RecordRole::WorkflowError).toBool();
    row.dotColor = statusFor(index.data(RecordRole::State).toString()).color;
    return row;
}
} // namespace aster::ui
