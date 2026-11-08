// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QKeyEvent>
#include <QSet>
#include "ui/workflow/workflow_layout.h"
#include "ui/workflow/node_edit_dialog.h"
#include <QDragLeaveEvent>
#include "ui/settings/theme_manager.h"
#include "ui/workflow/workflow_canvas.h"
#include "ui/common/presentation.h"
#include "ui/common/popup_placement.h"
#include "ui/panels/joint_input_dialog.h"
#include <QGraphicsItem>
#include <QMouseEvent>
#include <QScrollBar>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <algorithm>
#include <cmath>
namespace aster::ui
{
namespace
{
class Card : public QGraphicsItem
{
  public:
    QRectF rect;
    QString title, detail, badge;
    bool selected = false, source = false, ghost = false;
    QRectF boundingRect() const override
    {
        return rect.adjusted(-3, -3, 3, 3);
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(themeColor(ghost      ? "accent"
                                  : selected ? "accent"
                                             : "border"),
                       selected || ghost ? 2 : 1, Qt::SolidLine));
        p->setBrush(themeColor(ghost ? "hover" : "surface"));
        p->drawRoundedRect(rect, 6, 6);
        if (ghost)
        {
            p->setPen(themeColor("accent"));
            QFont f = p->font();
            f.setPointSize(10);
            f.setBold(true);
            p->setFont(f);
            p->drawText(rect, Qt::AlignCenter, title);
            return;
        }
        auto color = themeColor(source ? "success" : selected ? "accent" : "controlBorder");
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        p->drawRoundedRect(QRectF(rect.left() + 13, rect.top() + 16, 4, 23), 2, 2);
        p->setPen(themeColor("text"));
        QFont f = p->font();
        f.setPointSizeF(10);
        f.setBold(true);
        p->setFont(f);
        const auto titleRect = rect.adjusted(27, 13, -10, -40);
        p->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                    p->fontMetrics().elidedText(title, Qt::ElideRight, int(titleRect.width())));
        f.setBold(false);
        f.setPointSizeF(8.5);
        p->setFont(f);
        p->setPen(themeColor("muted"));
        const auto detailRect = rect.adjusted(27, 40, -10, -11);
        p->drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter,
                    p->fontMetrics().elidedText(detail, Qt::ElideRight, int(detailRect.width())));
        if (!badge.isEmpty())
        {
            p->setPen(color);
            p->drawText(rect.adjusted(10, 0, -12, -6), Qt::AlignBottom | Qt::AlignRight, badge);
        }
    }
};
} // namespace
WorkflowCanvas::WorkflowCanvas(WorkflowDocument *d, QWidget *p) : QGraphicsView(p), m_document(d)
{
    setObjectName("workflowCanvas");
    setScene(new QGraphicsScene(this));
    setFrameShape(QFrame::NoFrame);
    setAcceptDrops(true);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
    setBackgroundBrush(themeColor("canvas"));
    setViewportUpdateMode(BoundingRectViewportUpdate);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this,
            [this]
            {
                rebuild();
                viewport()->update();
            });
    rebuild();
}
void WorkflowCanvas::setSelectedNode(NodeId id)
{
    m_selected = id;
    rebuild();
}
void WorkflowCanvas::setPreviewPair(QUuid pair)
{
    const auto joint=m_document->pairNodeFor(m_selected);
    if(joint.isNull() || m_previewPairs.value(joint)==pair)return;
    m_previewPairs[joint]=pair;rebuild();
}
void WorkflowCanvas::rebuild()
{
    const auto center = mapToScene(viewport()->rect().center());
    scene()->clear();
    m_dropHighlight = nullptr;
    m_insertions.clear();
    m_rects.clear();
    m_slots.clear();
    const auto &definition = m_document->model().definition();
    if (!m_document->node(m_selected))
        m_selected = {};
    QMap<NodeId, QStringList> selectedOutputs;
    const auto markOutput = [&selectedOutputs](NodeId node, const QString& output) {
        if (!selectedOutputs[node].contains(output)) selectedOutputs[node].append(output);
    };
    for (const auto *node = m_document->node(m_selected); node && node->input;
         node = m_document->node(node->input->parent))
    {
        if (m_document->model().registry().find(node->method)->descriptor.pairedInput) break;
        markOutput(node->input->parent, node->input->output);
    }
    QSet<NodeId> jointNodes, jointParents, insertable, replaceable;
    for (const auto &n : definition.nodes)
        if (m_document->model().registry().find(n.method)->descriptor.pairedInput)
        { jointNodes.insert(n.id); if (n.input) jointParents.insert(n.input->parent); }
    for (const auto &n : definition.nodes)
        if (n.input && !jointNodes.contains(n.id) && !jointNodes.contains(n.input->parent))
        { insertable.insert(n.id); if (!jointParents.contains(n.id)) replaceable.insert(n.id); }
    for (auto it = m_previewPairs.begin(); it != m_previewPairs.end();)
        if (!jointNodes.contains(it.key())) it = m_previewPairs.erase(it); else ++it;
    m_rects = verticalWorkflowLayout(definition);
    const auto activeJoint=m_document->pairNodeFor(m_selected);
    const auto groups=m_document->grouping();
    auto pairs=groups.value(activeJoint);
    BatchPair activePair;
    if(!pairs.isEmpty()) {
        activePair=pairs.front();
        for(const auto& pair:pairs)if(pair.id==m_previewPairs.value(activeJoint)){activePair=pair;break;}
        const auto* joint=m_document->node(activeJoint);
        const auto box=m_rects.value(activeJoint);
        for(int side=0;side<2;++side) {
            const auto ref=side==0?activePair.a:activePair.b;
            const auto origin=ref.node.isNull()?joint->input->parent:ref.node;
            const bool available=jointMemberAvailable(*m_document,activeJoint,ref);
            const auto label=QString(side==0?"A":"B");
            const auto tip=label+" · "+jointMemberLabel(*m_document,activeJoint,ref);
            const auto color=themeColor(available?"text":"error");
            const auto targetY=box.top()+(side==0?28:62);
            QPointF end(box.left(),targetY);
            if(available) {
                markOutput(origin, ref.port);
                for (const auto* node = m_document->node(origin); node && node->input;
                     node = m_document->node(node->input->parent))
                    markOutput(node->input->parent, node->input->output);
                const auto from=m_rects.value(origin);
                const bool fromRight=from.center().x()>=box.center().x();
                const double direction=fromRight?1:-1;
                end.setX(fromRight?box.right():box.left());
                const QPointF start(fromRight?from.left():from.right(),from.center().y()+(side==0?-8:8));
                const double gap=16+side*8;
                const double sourceLane=start.x()-direction*gap,targetLane=end.x()+direction*gap;
                const double crossY=box.top()-52-side*12;
                QPainterPath path(start);path.lineTo(sourceLane,start.y());path.lineTo(sourceLane,crossY);path.lineTo(targetLane,crossY);path.lineTo(targetLane,end.y());path.lineTo(end);
                QPen pen(color,1.7,side==0?Qt::DashLine:Qt::DashDotLine);pen.setCosmetic(true);
                auto* line=scene()->addPath(path,pen);line->setZValue(-.6);line->setData(0,5);line->setData(1,origin);line->setData(2,activeJoint);line->setData(3,activePair.id);line->setToolTip(tip);
                auto* arrow=scene()->addPolygon(QPolygonF{end,end+QPointF(direction*7,-4),end+QPointF(direction*7,4)},Qt::NoPen,QBrush(color));arrow->setZValue(-.5);arrow->setData(0,5);
            }
            auto* badge=scene()->addSimpleText(available?label:label+"!");auto font=badge->font();font.setBold(true);font.setPointSizeF(9);badge->setFont(font);badge->setBrush(color);
            badge->setPos(end.x()==box.left()?end.x()-badge->boundingRect().width()-4:end.x()+4,end.y()-badge->boundingRect().height()-4);
            badge->setToolTip(available?tip:text("ui.joint_unavailable")+"\n"+tip);badge->setData(0,6);badge->setData(1,side);badge->setData(2,available);badge->setZValue(1);
        }
    }
    for (const auto &n : definition.nodes)
        if (n.input)
        {
            const auto a = m_rects.value(n.input->parent), b = m_rects.value(n.id);
            const QPointF start(a.center().x(), a.bottom()), end(b.center().x(), b.top());
            const double bend = b.top() - 30;
            QPainterPath path(start);
            path.lineTo(start.x(), bend);
            path.lineTo(end.x(), bend);
            path.lineTo(end);
            const bool jointEdge=jointNodes.contains(n.id);
            auto *edge = scene()->addPath(path, QPen(themeColor("controlBorder"), jointEdge?1:1.6,jointEdge?Qt::DotLine:Qt::SolidLine));
            if(jointEdge)edge->setToolTip(text("ui.joint_placement_edge"));
            edge->setZValue(-1);
            scene()
                ->addPolygon(QPolygonF{end, end + QPointF(-3, -7), end + QPointF(3, -7)}, Qt::NoPen,
                             QBrush(themeColor("controlBorder")))
                ->setZValue(-1);
        }
    if (m_editable && definition.nodes.size() < WorkflowNodeLimit)
        for (const auto &n : definition.nodes)
        {
            if (!insertable.contains(n.id)) continue;
            const auto child = m_rects.value(n.id);
            const QRectF rect(child.center().x() - 12, child.top() - 34, 24, 22);
            m_insertions[n.id] = rect;
            auto *control = new Card;
            control->rect = rect; control->ghost = true; control->title = "+";
            control->setToolTip(text("ui.insert_node_hint"));
            control->setData(0, 4); control->setData(1, n.id);
            control->setAcceptedMouseButtons(Qt::NoButton);
            control->setCursor(Qt::PointingHandCursor); control->setZValue(2);
            scene()->addItem(control);
        }
    int ordinal=0;
    for (const auto &n : definition.nodes)
    {
        auto *card = new Card;
        card->rect = m_rects[n.id];
        card->selected = n.id == m_selected;
        card->source = !n.input;
        const auto *registration = m_document->model().registry().find(n.method);
        card->title = methodLabel(registration->descriptor);
        card->detail = portLabel(registration->descriptor, registration->descriptor.defaultOutput);
        if (registration->descriptor.outputs.size() > 1)
        {
            QStringList ports;
            if (selectedOutputs.contains(n.id))
                for (const auto& key : selectedOutputs.value(n.id))
                    ports << portLabel(registration->descriptor, key);
            else
                for (const auto &output : registration->descriptor.outputs)
                    ports << portLabel(registration->descriptor, output.key);
            card->detail = ports.join(" · ");
        }
        if (n.method == "mtm")
            card->detail = QString("NW %1  ·  MTM").arg(n.parameters["nw"].toDouble());
        else if (n.method == "detrend_lowess")
            card->detail = text("ui.window") + QString::number(n.parameters["fraction"].toDouble());
        else if (n.method == "file_source")
            card->detail = text("ui.selected") + QString::number(m_document->files(n.id).size()) +
                           text("ui.files");
        else if (n.method == "cwt")
            card->detail = n.parameters["mother"].toString() + "  ·  dj " +
                           QString::number(n.parameters["dj"].toDouble());
        if(registration->descriptor.pairedInput)
            card->detail=text("ui.joint_canvas_pairs").arg(groups.value(n.id).size());
        card->setToolTip(QString("#%1 · %2").arg(++ordinal).arg(card->title) + "\n" + card->detail +
            (replaceable.contains(n.id)
                ? "\n" + text("ui.replace_node_hint") : QString()));
        card->setData(0, 1);
        card->setData(1, n.id);
        card->setAcceptedMouseButtons(Qt::NoButton);
        scene()->addItem(card);
    }
    if (m_editable && definition.nodes.size() < WorkflowNodeLimit)
    {
        QSet<QPair<int, int>> occupied;
        for (const auto &p : definition.layout)
            occupied.insert({p.column, p.row});
        QSet<int> acceptedKinds;
        const auto &registry = m_document->model().registry();
        for (const auto &d : registry.descriptors())
            if (d.catalogVisible && d.inputKind)
                acceptedKinds.insert(int(*d.inputKind));
        for (const auto &node : definition.nodes)
        {
            const auto *entry = registry.find(node.method);
            if (!entry)
                continue;
            bool compatible = false;
            for (const auto &port : entry->descriptor.outputs)
                compatible |= acceptedKinds.contains(int(port.kind));
            if (!compatible)
                continue;
            QVector<ChildSlot> available;
            // Prefer a continuation for the compact, always-visible plus control.
            for (auto slot : {ChildSlot::Down, ChildSlot::Left, ChildSlot::Right})
            {
                const auto p = adjacentPosition(definition.layout.value(node.id), slot);
                if (p && !occupied.contains({p->column, p->row}))
                    available.push_back(slot);
            }
            if (available.isEmpty())
                continue;
            const auto rect = m_rects.value(node.id);
            const bool selected = node.id == m_selected;
            for (auto slot : available)
            {
                if (!selected && slot != available.front())
                    continue;
                const int column = slot == ChildSlot::Left ? 0 : slot == ChildSlot::Down ? 1 : 2;
                const QRectF target =
                    selected ? QRectF(rect.left() + column * 68, rect.bottom() + 6, 62, 28)
                             : QRectF(rect.right() - 34, rect.bottom() + 6, 34, 28);
                if (selected)
                    m_slots.insert(int(slot), target);
                auto *control = new Card;
                control->rect = target;
                control->ghost = true;
                control->title = selected ? text(slot == ChildSlot::Left    ? "ui.add_left_branch"
                                                 : slot == ChildSlot::Right ? "ui.add_right_branch"
                                                                            : "ui.add_next")
                                          : "+";
                control->setToolTip(text("ui.add_to_node").arg(methodLabel(entry->descriptor)));
                control->setData(0, 2);
                control->setData(1, int(slot));
                control->setData(2, node.id);
                control->setAcceptedMouseButtons(Qt::NoButton);
                control->setCursor(Qt::PointingHandCursor);
                control->setZValue(2);
                scene()->addItem(control);
            }
        }
    }
    if (definition.nodes.isEmpty())
    {
        auto *start = new Card;
        start->rect = {0, 0, 260, 100};
        start->title = text("ui.choose_a_source");
        start->ghost = true;
        start->setData(0, 3);
        scene()->addItem(start);
        auto *title = scene()->addText(text("ui.build_a_workflow"));
        auto font = title->font();
        font.setPointSizeF(19);
        font.setBold(true);
        title->setFont(font);
        title->setDefaultTextColor(themeColor("muted"));
        title->setPos((260 - title->boundingRect().width()) / 2, -74);
        auto *hint = scene()->addText(text("ui.import_files_or_generate_a_new_series"));
        hint->setDefaultTextColor(themeColor("muted"));
        hint->setPos((260 - hint->boundingRect().width()) / 2, 122);
    }
    setSceneRect(scene()->itemsBoundingRect().adjusted(-420, -240, 420, 240));
    centerOn(center);
    viewport()->update();
}
QPoint WorkflowCanvas::slotViewportPosition(ChildSlot slot) const
{
    return mapFromScene(m_slots.value(int(slot)).center());
}
void WorkflowCanvas::fitWorkflow()
{
    auto bounds = scene()->itemsBoundingRect().adjusted(-35, -35, 35, 35);
    fitInView(bounds, Qt::KeepAspectRatio);
    if (transform().m11() > 1.05)
    {
        resetTransform();
        scale(1.05, 1.05);
    }
    centerOn(bounds.center());
    emit zoomChanged(qRound(transform().m11() * 100));
}
void WorkflowCanvas::drawBackground(QPainter *p, const QRectF &r)
{
    p->fillRect(r, themeColor("canvas"));
    // Keep the work proportional to viewport pixels even after fitting distant
    // roots. A fixed scene-space grid can otherwise require billions of dots.
    const double zoom = std::hypot(p->worldTransform().m11(), p->worldTransform().m12());
    if (!(zoom > 0) || !std::isfinite(zoom))
        return;
    const double spacing = 24 * std::exp2(std::max(0., std::ceil(std::log2(12 / (24 * zoom)))));
    QPen pen(themeColor("grid"), 1);
    pen.setCosmetic(true);
    p->setPen(pen);
    for (double x = std::floor(r.left() / spacing) * spacing; x < r.right(); x += spacing)
        for (double y = std::floor(r.top() / spacing) * spacing; y < r.bottom(); y += spacing)
            p->drawPoint(QPointF(x, y));
}
void WorkflowCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier)
    {
        if (!event->isAutoRepeat() && m_editable && !m_selected.isNull())
            emit removeRequested(m_selected);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}
void WorkflowCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
    {
        auto *item = itemAt(e->pos());
        if (item)
        {
            int kind = item->data(0).toInt();
            if (kind == 1)
            {
                auto id = item->data(1).toUuid();
                emit nodeSelected(id);
                return;
            }
            if (kind == 2 && m_editable)
            {
                auto slot = static_cast<ChildSlot>(item->data(1).toInt());
                emit slotActivated(item->data(2).toUuid(), slot, viewport()->mapToGlobal(e->pos()));
                return;
            }
            if (kind == 4 && m_editable)
            {
                emit insertRequested(item->data(1).toUuid(), viewport()->mapToGlobal(e->pos()));
                return;
            }
            if (kind == 3 && m_editable)
            {
                emit rootRequested(viewport()->mapToGlobal(e->pos()));
                return;
            }
        }
    }
    if (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton)
    {
        m_panning = true;
        m_last = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}
void WorkflowCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_panning)
    {
        auto delta = e->pos() - m_last;
        m_last = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}
void WorkflowCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning)
    {
        m_panning = false;
        unsetCursor();
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}
void WorkflowCanvas::wheelEvent(QWheelEvent *e)
{
    const double current = transform().m11();
    const double next =
        std::clamp(current * std::pow(1.0015, e->angleDelta().y()), std::min(.25, current), 2.);
    if (current > 0 && next != current)
    {
        scale(next / current, next / current);
        emit zoomChanged(qRound(next * 100));
    }
    e->accept();
}
void WorkflowCanvas::contextMenuEvent(QContextMenuEvent *e)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *item = itemAt(e->pos());
    if (item && item->data(0).toInt() == 1 && m_editable)
    {
        const auto id = item->data(1).toUuid();
        auto *replace = menu->addAction(text("ui.replace_node") + QStringLiteral("…"), this,
            [this, id, position = e->globalPos()] { emit replaceRequested(id, position); });
        replace->setData("replace_node");
        const auto reason = nodeRewriteAvailability(m_document->model(), NodeRewriteKind::Replace, id);
        replace->setEnabled(reason.isEmpty()); replace->setToolTip(nodeRewriteMessage(reason));
        menu->setToolTipsVisible(true);
        menu->addAction(text("ui.delete_task_and_descendants"), this,
                        [this, id] { emit removeRequested(id); });
    }
    menu->addAction(text("ui.fit_workflow"), this, &WorkflowCanvas::fitWorkflow);
    showMenuWithinWindow(menu, this, QRect(e->globalPos(), QSize(1, 1)));
}
bool WorkflowCanvas::dropTarget(const QPoint &pos, const QString &method, NodeId &parent,
                                ChildSlot &slot, int &rewrite) const
{
    rewrite = 0;
    if (!m_editable || m_document->editingLocked())
        return false;
    auto *item = itemAt(pos);
    const auto *dragged = m_document->model().registry().find(method);
    if (!dragged || !dragged->descriptor.catalogVisible)
        return false;
    if (!dragged->descriptor.inputKind)
    {
        parent = {};
        slot = ChildSlot::Down;
        return !item || item->data(0).toInt() == 3;
    }
    if (item && (item->data(0).toInt() == 1 || item->data(0).toInt() == 4))
    {
        const bool replacement = item->data(0).toInt() == 1;
        parent = item->data(1).toUuid();
        if (!inspectNodeRewrite(m_document->model(), replacement ? NodeRewriteKind::Replace : NodeRewriteKind::InsertBefore,
                                parent, method).ok()) return false;
        rewrite = replacement ? 2 : 1;
        return true;
    }
    if (!item || item->data(0).toInt() != 2)
        return false;
    parent = item->data(2).toUuid();
    slot = static_cast<ChildSlot>(item->data(1).toInt());
    const auto *n = m_document->node(parent);
    const auto *child = m_document->model().registry().find(method);
    if (!n || !child || !child->descriptor.inputKind)
        return false;
    const auto *p = m_document->model().registry().find(n->method);
    for (const auto &port : p->descriptor.outputs)
        if (port.kind == *child->descriptor.inputKind)
            if (m_document->model().previewChild(parent, slot, method, {}, port.key).ok())
                return true;
    return false;
}
void WorkflowCanvas::dragEnterEvent(QDragEnterEvent *e)
{
    if (m_editable && e->mimeData()->hasFormat(TaskMimeType))
        e->acceptProposedAction();
}
void WorkflowCanvas::clearDropHighlight()
{
    delete m_dropHighlight; m_dropHighlight = nullptr;
}
void WorkflowCanvas::dragMoveEvent(QDragMoveEvent *e)
{
    NodeId p; ChildSlot s; int rewrite = 0;
    const bool valid = dropTarget(e->position().toPoint(), QString::fromUtf8(e->mimeData()->data(TaskMimeType)), p, s, rewrite);
    clearDropHighlight();
    if (valid)
    {
        e->acceptProposedAction();
        if (rewrite)
        {
            const auto rect = rewrite == 2 ? m_rects.value(p) : m_insertions.value(p);
            m_dropHighlight = scene()->addRect(rect.adjusted(-3, -3, 3, 3),
                QPen(themeColor("accent"), 1.5, Qt::DashLine), Qt::NoBrush);
            m_dropHighlight->setZValue(-.5);
        }
    }
    else e->ignore();
}
void WorkflowCanvas::dragLeaveEvent(QDragLeaveEvent *e)
{
    clearDropHighlight(); e->accept();
}
void WorkflowCanvas::dropEvent(QDropEvent *e)
{
    NodeId p; ChildSlot s; int rewrite = 0;
    const auto method = QString::fromUtf8(e->mimeData()->data(TaskMimeType));
    const bool valid = dropTarget(e->position().toPoint(), method, p, s, rewrite);
    clearDropHighlight();
    if (valid)
    {
        e->acceptProposedAction();
        if (rewrite == 1) emit insertMethodDropped(method, p);
        else if (rewrite == 2) emit replaceMethodDropped(method, p);
        else if (p.isNull()) emit sourceDropped(method);
        else emit methodDropped(method, p, s);
    }
    else e->ignore();
}
} // namespace aster::ui
