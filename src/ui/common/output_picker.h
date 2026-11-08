// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_OUTPUT_PICKER_H
#define ASTER_UI_OUTPUT_PICKER_H
#include <QFrame>
#include <QPointer>
#include <QVector>
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
namespace aster::ui
{
struct OutputChoice
{
    QString key, label;
    bool preferred = false;
};
// A port chooser only: opening, highlighting, filtering or cancelling never edits a workflow.
class OutputPicker final : public QFrame
{
    Q_OBJECT
  public:
    OutputPicker(QWidget *owner, QString title, QString destination,
                 const QVector<OutputChoice> &choices);
    void openAt(const QRect &anchor);
  signals:
    void chosen(QString key);
  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
  private:
    void choose(QListWidgetItem *item);
    QPointer<QWidget> m_owner;
    QPointer<QWidget> m_previousFocus;
    QListWidget *m_list;
    QLineEdit *m_search;
    QLabel *m_empty;
    int m_rowHeight = 36;
    bool m_committed = false;
    bool m_dismissed = false;
};
} // namespace aster::ui
#endif
