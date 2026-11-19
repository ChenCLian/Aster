// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_FILE_LIST_DELEGATE_H
#define ASTER_UI_FILE_LIST_DELEGATE_H
#include <QStyledItemDelegate>
#include <QListWidgetItem>
class QListView;
namespace aster::ui
{
struct FileListStatus
{
    const char *label;
    const char *color = "muted";
    bool hollow = false;
};

// The model keeps the plain fields. Qt requests the formatted card only on hover.
class FileListItem final : public QListWidgetItem
{
  public:
    struct Details
    {
        QString source, record, notes;
        bool workflowError = false;
    };
    FileListItem(const QString &name, QListWidget *list, Details details, FileListStatus status);
    void setStatus(FileListStatus status);
    QVariant data(int role) const override;
  private:
    Details m_details;
    FileListStatus m_status;
};

class FileListDelegate : public QStyledItemDelegate
{
  public:
    explicit FileListDelegate(QListView *view);
    void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const final;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const final;
    static QRect checkRect(const QRect &row);

  protected:
    struct Row
    {
        QString name;
        const char *dotColor = "muted";
        bool hollowDot = false;
        bool highlighted = false;
        bool current = false;
        bool workflowError = false;
        bool showCheckbox = false;
    };
    virtual Row describe(const QStyleOptionViewItem &option, const QModelIndex &index) const = 0;
    QListView *const m_view;

  private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool m_keyboardFocus = false;
};
} // namespace aster::ui
#endif
