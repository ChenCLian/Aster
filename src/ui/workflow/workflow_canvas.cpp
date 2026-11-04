#include <QKeyEvent>
#include <QSet>
#include "ui/workflow/workflow_layout.h"
#include "ui/settings/theme_manager.h"
#include "ui/workflow/workflow_canvas.h"
#include "ui/common/presentation.h"
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
void WorkflowCanvas::rebuild()
{
    const auto center = mapToScene(viewport()->rect().center());
    scene()->clear();
    m_rects.clear();
    m_slots.clear();
    const auto &definition = m_document->model().definition();
    if (!m_document->node(m_selected))
        m_selected = {};
    m_rects = verticalWorkflowLayout(definition);
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
            auto *edge = scene()->addPath(path, QPen(themeColor("controlBorder"), 1.6));
            edge->setZValue(-1);
            scene()
                ->addPolygon(QPolygonF{end, end + QPointF(-3, -7), end + QPointF(3, -7)}, Qt::NoPen,
                             QBrush(themeColor("controlBorder")))
                ->setZValue(-1);
        }
    for (const auto &n : definition.nodes)
    {
        auto *card = new Card;
        card->rect = m_rects[n.id];
        card->selected = n.id == m_selected;
        card->source = !n.input;
        const auto *registration = m_document->model().registry().find(n.method);
        card->title = methodLabel(registration->descriptor);
        card->detail = n.method;
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
        card->setToolTip(card->title + "\n" + card->detail);
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
        menu->addAction(text("ui.delete_task_and_descendants"), this,
                        [this, id] { emit removeRequested(id); });
    }
    menu->addAction(text("ui.fit_workflow"), this, &WorkflowCanvas::fitWorkflow);
    menu->popup(e->globalPos());
}
bool WorkflowCanvas::dropTarget(const QPoint &pos, const QString &method, NodeId &parent,
                                ChildSlot &slot) const
{
    if (!m_editable)
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
void WorkflowCanvas::dragMoveEvent(QDragMoveEvent *e)
{
    NodeId p;
    ChildSlot s;
    if (dropTarget(e->position().toPoint(), QString::fromUtf8(e->mimeData()->data(TaskMimeType)), p,
                   s))
        e->acceptProposedAction();
    else
        e->ignore();
}
void WorkflowCanvas::dropEvent(QDropEvent *e)
{
    NodeId p;
    ChildSlot s;
    const auto method = QString::fromUtf8(e->mimeData()->data(TaskMimeType));
    if (dropTarget(e->position().toPoint(), method, p, s))
    {
        e->acceptProposedAction();
        if (p.isNull())
            emit sourceDropped(method);
        else
            emit methodDropped(method, p, s);
    }
    else
        e->ignore();
}
} // namespace aster::ui
