// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_WORKFLOW_CANVAS_H
#define ASTER_UI_WORKFLOW_CANVAS_H
#include "ui/workflow/workflow_document.h"
#include <QGraphicsView>
namespace aster::ui
{
inline constexpr const char *TaskMimeType = "application/x-aster-method";
class WorkflowCanvas : public QGraphicsView
{
    Q_OBJECT
  public:
    explicit WorkflowCanvas(WorkflowDocument *document, QWidget *parent = nullptr);
    void setSelectedNode(NodeId id);
    NodeId selectedNode() const
    {
        return m_selected;
    }
    void setEditable(bool editable)
    {
        m_editable = editable;
        rebuild();
    }
    QRectF nodeRect(const NodeId &id) const
    {
        return m_rects.value(id);
    }
    QPoint slotViewportPosition(ChildSlot slot) const;
    void rebuild();
    void fitWorkflow();
  signals:
    void nodeSelected(aster::NodeId id);
    void slotActivated(aster::NodeId parent, aster::ChildSlot slot, QPoint globalPosition);
    void methodDropped(QString method, aster::NodeId parent, aster::ChildSlot slot);
    void rootRequested(QPoint globalPosition);
    void sourceDropped(QString method);
    void removeRequested(aster::NodeId id);
    void zoomChanged(int percent);

  protected:
    void keyPressEvent(QKeyEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    bool dropTarget(const QPoint &point, const QString &method, NodeId &parent,
                    ChildSlot &slot) const;
    WorkflowDocument *m_document;
    NodeId m_selected;
    QMap<NodeId, QRectF> m_rects;
    QMap<int, QRectF> m_slots;
    bool m_editable = true, m_panning = false;
    QPoint m_last;
};
} // namespace aster::ui
#endif
