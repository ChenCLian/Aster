// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_WORKFLOW_CANVAS_H
#define ASTER_UI_WORKFLOW_CANVAS_H
#include "ui/workflow/workflow_document.h"
#include <QGraphicsView>
class QGraphicsRectItem;
class QDragLeaveEvent;
namespace aster::ui
{
inline constexpr const char *TaskMimeType = "application/x-aster-method";
class WorkflowCanvas : public QGraphicsView
{
    Q_OBJECT
  public:
    explicit WorkflowCanvas(WorkflowDocument *document, QWidget *parent = nullptr);
    void setSelectedNode(NodeId id);
    void setPreviewPair(QUuid pair);
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
    QPoint insertionViewportPosition(NodeId child) const { return mapFromScene(m_insertions.value(child).center()); }
    void rebuild();
    void fitWorkflow();
  signals:
    void nodeSelected(aster::NodeId id);
    void slotActivated(aster::NodeId parent, aster::ChildSlot slot, QPoint globalPosition);
    void methodDropped(QString method, aster::NodeId parent, aster::ChildSlot slot);
    void rootRequested(QPoint globalPosition);
    void sourceDropped(QString method);
    void removeRequested(aster::NodeId id);
    void insertRequested(aster::NodeId child, QPoint globalPosition);
    void replaceRequested(aster::NodeId node, QPoint globalPosition);
    void insertMethodDropped(QString method, aster::NodeId child);
    void replaceMethodDropped(QString method, aster::NodeId node);
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
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    bool dropTarget(const QPoint &point, const QString &method, NodeId &parent,
                    ChildSlot &slot, int &rewrite) const;
    void clearDropHighlight();
    WorkflowDocument *m_document;
    NodeId m_selected;
    QMap<NodeId, QUuid> m_previewPairs;
    QMap<NodeId, QRectF> m_rects;
    QMap<int, QRectF> m_slots;
    QMap<NodeId, QRectF> m_insertions;
    QGraphicsRectItem *m_dropHighlight = nullptr;
    bool m_editable = true, m_panning = false;
    QPoint m_last;
};
} // namespace aster::ui
#endif
