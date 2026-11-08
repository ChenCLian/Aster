// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PLOT_LAYOUT_WIDGET_H
#define ASTER_PLOT_LAYOUT_WIDGET_H
#include "ui/results/plot_arrangement.h"
#include <QScrollArea>
#include <QPointer>
class QSplitter;
class QPushButton;
class QMimeData;
namespace aster::ui
{
// Owns geometry only. Existing plot panels survive every rearrangement.
class PlotLayoutWidget : public QScrollArea
{
    Q_OBJECT
  public:
    explicit PlotLayoutWidget(QWidget *parent = nullptr);
    QPushButton *createTitle(QUuid id, const QString &label);
    bool arrange(const PlotArrangement &layout, const QMap<QUuid, QWidget *> &panels,
                 const QVector<QUuid> &defaultOrder);
    PlotArrangement snapshot() const;
    bool moveCell(QUuid source, QUuid target, PlotPlacement placement, bool wholePage = false);
    void resetArrangement();
    QMimeData *dragData(QUuid cell) const;
    void titleMenu(QUuid cell, QPoint position);
  signals:
    void layoutChanged(bool custom);
    void selectionRequested(QUuid cell);
  protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;
  private:
    struct Split { QVector<int> path; QPointer<QSplitter> widget; };
    struct Drop
    {
        QUuid source, target;
        PlotPlacement placement = PlotPlacement::Swap;
        bool wholePage = false, valid = false;
        PlotArrangement layout;
    };
    QWidget *makeTree(const PlotArrangement &node, QVector<int> path);
    void applyFractions();
    QUuid draggedCell(const QMimeData *) const;
    Drop dropAt(const QMimeData *, QPoint) const;
    QMap<QUuid, QWidget *> livePanels() const;
    void showDrop(const Drop &drop);
    QWidget *m_canvas;
    QPointer<QWidget> m_root;
    QWidget *m_indicator;
    PlotArrangement m_layout;
    QVector<QUuid> m_defaultOrder;
    QMap<QUuid, QPointer<QWidget>> m_panels;
    QVector<Split> m_splits;
    QUuid m_nonce = QUuid::createUuid();
    quint64 m_generation = 0;
    bool m_applying = false;
};
}
#endif
