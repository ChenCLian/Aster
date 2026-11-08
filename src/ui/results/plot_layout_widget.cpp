// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/results/plot_layout_widget.h"
#include "ui/common/presentation.h"
#include <QApplication>
#include <QPushButton>
#include <QSplitter>
#include <QSplitterHandle>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QPainter>
#include <QMenu>
#include <QTimer>
namespace aster::ui
{
namespace
{
constexpr auto mimeType = "application/x-aster-subplot-layout";
class Divider final : public QSplitterHandle
{
  public:
    Divider(Qt::Orientation orientation, QSplitter *parent) : QSplitterHandle(orientation, parent)
    {
        setAttribute(Qt::WA_Hover);
        setToolTip(text("ui.plot_divider_help"));
    }
  protected:
    void enterEvent(QEnterEvent *event) override { QSplitterHandle::enterEvent(event); update(); }
    void leaveEvent(QEvent *event) override { QSplitterHandle::leaveEvent(event); update(); }
    void mousePressEvent(QMouseEvent *event) override
    { m_pressed = event->button() == Qt::LeftButton; QSplitterHandle::mousePressEvent(event); update(); }
    void mouseReleaseEvent(QMouseEvent *event) override
    { m_pressed = false; QSplitterHandle::mouseReleaseEvent(event); update(); }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) { QSplitterHandle::mouseDoubleClickEvent(event); return; }
        const auto sizes = splitter()->sizes();
        if (sizes.size() != 2) return;
        const int total = sizes[0] + sizes[1];
        splitter()->setSizes({total / 2, total - total / 2});
        emit splitter()->splitterMoved(total / 2, 1);
        event->accept();
    }
    void paintEvent(QPaintEvent *) override
    {
        if (!underMouse() && !m_pressed) return;
        QPainter painter(this);
        auto color = palette().color(QPalette::Highlight); color.setAlpha(180);
        painter.setPen(QPen(color, 2));
        if (orientation() == Qt::Horizontal)
            painter.drawLine(width() / 2, 3, width() / 2, height() - 3);
        else painter.drawLine(3, height() / 2, width() - 3, height() / 2);
    }
  private:
    bool m_pressed = false;
};
class PlotSplitter final : public QSplitter
{
  public:
    PlotSplitter(Qt::Orientation orientation, QWidget *parent) : QSplitter(orientation, parent)
    {
        setObjectName("plotDivider");
        setChildrenCollapsible(false);
        setOpaqueResize(false);
        setHandleWidth(9);
    }
  protected:
    QSplitterHandle *createHandle() override { return new Divider(orientation(), this); }
};
class DropIndicator final : public QWidget
{
  public:
    explicit DropIndicator(QWidget *parent) : QWidget(parent)
    {
        setObjectName("plotDropPreview");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }
    QString label;
  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        auto color = palette().color(QPalette::Highlight);
        painter.setPen(QPen(color, 3));
        color.setAlpha(45); painter.setBrush(color);
        painter.drawRect(rect().adjusted(2, 2, -2, -2));
        painter.setPen(palette().color(QPalette::Text));
        auto font = painter.font(); font.setBold(true); painter.setFont(font);
        const auto box = painter.boundingRect(rect().adjusted(8, 8, -8, -8),
                                              Qt::AlignCenter | Qt::TextWordWrap, label);
        painter.fillRect(box.adjusted(-6, -4, 6, 4), palette().color(QPalette::Window));
        painter.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, label);
    }
};
class PlotTitle final : public QPushButton
{
  public:
    PlotTitle(PlotLayoutWidget *owner, QUuid cell, const QString &label)
        : QPushButton(label), m_owner(owner), m_cell(cell)
    {
        setObjectName("selectSubplot");
        setCheckable(true);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setToolTip(aster::ui::text("ui.plot_title_drag_help"));
        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this, &QWidget::customContextMenuRequested, this, [this](QPoint pos) {
            if (m_owner) m_owner->titleMenu(m_cell, mapToGlobal(pos));
        });
    }
  protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        m_press = event->position().toPoint();
        m_armed = event->button() == Qt::LeftButton;
        QPushButton::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    { m_armed = false; QPushButton::mouseReleaseEvent(event); }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_armed || !(event->buttons() & Qt::LeftButton) || !m_owner ||
            (event->position().toPoint() - m_press).manhattanLength() < QApplication::startDragDistance())
        { QPushButton::mouseMoveEvent(event); return; }
        m_armed = false;
        QPointer<PlotTitle> guard(this);
        emit m_owner->selectionRequested(m_cell);
        if (!guard || !m_owner) return;
        auto *data = m_owner->dragData(m_cell);
        if (!data) return;
        setDown(false);
        auto *drag = new QDrag(this);
        drag->setMimeData(data);
        // Only the small title is captured; dragging never rasterizes the data.
        auto pixmap = grab();
        const auto dpr = pixmap.devicePixelRatio();
        if (pixmap.width() / dpr > 360)
            pixmap = pixmap.scaledToWidth(qRound(360 * dpr), Qt::SmoothTransformation);
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(qRound(pixmap.width() / (2 * dpr)), qRound(pixmap.height() / (2 * dpr))));
        QPointer<QDrag> dragGuard(drag);
        drag->exec(Qt::MoveAction);
        if (guard) guard->setDown(false);
        if (dragGuard) dragGuard->deleteLater();
    }
  private:
    QPointer<PlotLayoutWidget> m_owner;
    QUuid m_cell;
    QPoint m_press;
    bool m_armed = false;
};
PlotArrangement *atPath(PlotArrangement &tree, const QVector<int> &path)
{
    auto *node = &tree;
    for (int index : path)
    {
        if (index < 0 || index >= int(node->children.size())) return nullptr;
        node = &node->children[size_t(index)];
    }
    return node;
}
}
PlotLayoutWidget::PlotLayoutWidget(QWidget *parent) : QScrollArea(parent)
{
    setObjectName("plotLayout");
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    viewport()->setAcceptDrops(true);
    m_canvas = new QWidget;
    auto *layout = new QVBoxLayout(m_canvas);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    setWidget(m_canvas);
    m_indicator = new DropIndicator(viewport());
}
QPushButton *PlotLayoutWidget::createTitle(QUuid id, const QString &label)
{
    auto *button = new PlotTitle(this, id, label);
    // Reassert exclusive selection after the button toggles on release.
    // A drag selects at its threshold instead, avoiding two updates per click.
    connect(button, &QPushButton::clicked, this, [this, id] { emit selectionRequested(id); });
    return button;
}
QMap<QUuid, QWidget *> PlotLayoutWidget::livePanels() const
{
    QMap<QUuid, QWidget *> result;
    for (auto it = m_panels.cbegin(); it != m_panels.cend(); ++it)
        if (it.value()) result[it.key()] = it.value();
    return result;
}
PlotArrangement PlotLayoutWidget::snapshot() const
{
    auto tree = m_layout;
    for (const auto &entry : m_splits)
        if (entry.widget)
        {
            const auto sizes = entry.widget->sizes();
            auto *node = atPath(tree, entry.path);
            if (node && sizes.size() == 2 && sizes[0] > 0 && sizes[1] > 0)
                node->fraction = double(sizes[0]) / (sizes[0] + sizes[1]);
        }
    return tree;
}
bool PlotLayoutWidget::arrange(const PlotArrangement &tree, const QMap<QUuid, QWidget *> &panels,
                              const QVector<QUuid> &defaultOrder)
{
    const auto ids = tree.order();
    if (!tree.valid(true) || ids.size() != panels.size() || ids.size() != defaultOrder.size() ||
        QSet<QUuid>(ids.cbegin(), ids.cend()) != QSet<QUuid>(defaultOrder.cbegin(), defaultOrder.cend())) return false;
    for (auto id : ids) if (!panels.value(id)) return false;
    ++m_generation;
    m_indicator->hide();
    m_applying = true;
    // Detach surviving panels before deleting the obsolete splitters. This
    // preserves PlotView identities, styles, zoom and pending result targets.
    for (auto *panel : panels) { panel->hide(); panel->setParent(m_canvas); }
    while (auto *item = m_canvas->layout()->takeAt(0)) delete item;
    if (qobject_cast<QSplitter *>(m_root.data())) delete m_root;
    m_root = nullptr;
    m_splits.clear(); m_panels.clear();
    for (auto it = panels.cbegin(); it != panels.cend(); ++it) m_panels[it.key()] = it.value();
    m_layout = tree; m_defaultOrder = defaultOrder;
    if (!tree.empty())
    {
        m_root = makeTree(tree, {});
        m_canvas->layout()->addWidget(m_root);
        m_root->show();
        m_canvas->layout()->activate();
        applyFractions();
    }
    m_applying = false;
    const auto generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != m_generation) return;
        m_applying = true;
        m_canvas->layout()->activate();
        applyFractions();
        m_applying = false;
    });
    return true;
}
QWidget *PlotLayoutWidget::makeTree(const PlotArrangement &node, QVector<int> path)
{
    if (!node.cell.isNull())
    {
        auto *panel = m_panels[node.cell].data();
        panel->show(); return panel;
    }
    auto *splitter = new PlotSplitter(node.orientation, m_canvas);
    m_splits.push_back({path, splitter});
    for (int i = 0; i < 2; ++i)
    {
        auto childPath = path; childPath.push_back(i);
        splitter->addWidget(makeTree(node.children[size_t(i)], childPath));
        splitter->setStretchFactor(i, 1);
    }
    connect(splitter, &QSplitter::splitterMoved, this, [this] {
        if (m_applying) return;
        ++m_generation; // A real resize supersedes any queued initial sizing.
        m_layout = snapshot();
        emit layoutChanged(true);
    });
    return splitter;
}
void PlotLayoutWidget::applyFractions()
{
    // Parents first: each descendant receives its final available size.
    for (const auto &entry : m_splits)
        if (auto *splitter = entry.widget.data())
        {
            auto *node = atPath(m_layout, entry.path);
            const int total = qMax(2, (splitter->orientation() == Qt::Horizontal
                ? splitter->width() : splitter->height()) - splitter->handleWidth());
            const int first = qRound(total * node->fraction);
            splitter->setSizes({first, total - first});
        }
}
bool PlotLayoutWidget::moveCell(QUuid source, QUuid target, PlotPlacement placement, bool wholePage)
{
    auto next = snapshot();
    if (!next.move(source, target, placement, wholePage)) return false;
    if (!arrange(next, livePanels(), m_defaultOrder)) return false;
    emit layoutChanged(true); return true;
}
void PlotLayoutWidget::resetArrangement()
{
    if (arrange(PlotArrangement::defaults(m_defaultOrder), livePanels(), m_defaultOrder))
        emit layoutChanged(false);
}
QMimeData *PlotLayoutWidget::dragData(QUuid id) const
{
    if (!m_panels.value(id) || m_panels.size() < 2) return nullptr;
    auto *mime = new QMimeData;
    mime->setData(mimeType, m_nonce.toRfc4122() + id.toRfc4122());
    return mime;
}
QUuid PlotLayoutWidget::draggedCell(const QMimeData *mime) const
{
    if (!mime || m_panels.size() < 2) return {};
    const auto bytes = mime->data(mimeType);
    if (bytes.size() != 32 || bytes.first(16) != m_nonce.toRfc4122()) return {};
    const auto cell = QUuid::fromRfc4122(bytes.last(16));
    return m_panels.value(cell) ? cell : QUuid();
}
PlotLayoutWidget::Drop PlotLayoutWidget::dropAt(const QMimeData *mime, QPoint point) const
{
    Drop drop; drop.source = draggedCell(mime);
    if (drop.source.isNull() || !viewport()->rect().contains(point)) return drop;
    const auto edges = [](QRect rect, QPoint p, double band) -> int {
        const int distances[] = {p.x() - rect.left(), rect.right() - p.x(),
                                 p.y() - rect.top(), rect.bottom() - p.y()};
        int edge = -1; double best = 1.;
        for (int i = 0; i < 4; ++i)
        {
            const double width = std::min(band, (i < 2 ? rect.width() : rect.height()) * .23);
            const double distance = distances[i] / qMax(1., width);
            if (distance < best) { best = distance; edge = i; }
        }
        return edge;
    };
    const PlotPlacement placements[] = {PlotPlacement::Left, PlotPlacement::Right,
                                        PlotPlacement::Above, PlotPlacement::Below};
    // Outer edges refer to the whole page; inner panel edges only split that
    // panel. A central drop swaps identities. Empty gaps are not drop targets.
    int edge = edges(viewport()->rect(), point, 14.);
    if (edge >= 0) { drop.wholePage = true; drop.placement = placements[edge]; }
    else
    {
        for (auto id : m_layout.order())
        {
            auto *panel = m_panels.value(id).data();
            if (!panel) continue;
            const QRect rect(panel->mapTo(viewport(), QPoint()), panel->size());
            if (!rect.contains(point)) continue;
            drop.target = id;
            edge = edges(rect, point, 60.);
            if (auto *title = panel->findChild<QPushButton *>("selectSubplot"))
            {
                const QRect titleRect(title->mapTo(viewport(), QPoint()), title->size());
                if (titleRect.contains(point)) edge = edges(titleRect, point, 12.);
            }
            if (edge >= 0) drop.placement = placements[edge];
            break;
        }
    }
    drop.layout = snapshot();
    drop.valid = drop.layout.move(drop.source, drop.target, drop.placement, drop.wholePage);
    return drop;
}
void PlotLayoutWidget::showDrop(const Drop &drop)
{
    if (!drop.valid) { m_indicator->hide(); return; }
    const auto area = drop.layout.regions().value(drop.source);
    const auto canvas = m_canvas->rect();
    const QRect rect(m_canvas->mapTo(viewport(), QPoint(qRound(area.x() * canvas.width()),
                                                      qRound(area.y() * canvas.height()))),
                     QSize(qRound(area.width() * canvas.width()), qRound(area.height() * canvas.height())));
    auto *indicator = static_cast<DropIndicator *>(m_indicator);
    indicator->label = text(drop.placement == PlotPlacement::Swap ? "ui.plot_drop_swap" :
        drop.wholePage ? "ui.plot_drop_page_edge" : "ui.plot_drop_panel_edge");
    indicator->setGeometry(rect.intersected(viewport()->rect()));
    indicator->show(); indicator->raise(); indicator->update();
}
void PlotLayoutWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (!draggedCell(event->mimeData()).isNull() && (event->possibleActions() & Qt::MoveAction))
    { event->setDropAction(Qt::MoveAction); event->accept(); }
    else event->ignore();
}
void PlotLayoutWidget::dragMoveEvent(QDragMoveEvent *event)
{
    const auto drop = dropAt(event->mimeData(), event->position().toPoint());
    showDrop(drop);
    if (drop.valid && (event->possibleActions() & Qt::MoveAction))
    { event->setDropAction(Qt::MoveAction); event->accept(); }
    else event->ignore();
}
void PlotLayoutWidget::dragLeaveEvent(QDragLeaveEvent *event)
{ m_indicator->hide(); event->accept(); }
void PlotLayoutWidget::dropEvent(QDropEvent *event)
{
    const auto drop = dropAt(event->mimeData(), event->position().toPoint());
    m_indicator->hide();
    if (drop.valid && (event->possibleActions() & Qt::MoveAction) &&
        moveCell(drop.source, drop.target, drop.placement, drop.wholePage))
    { event->setDropAction(Qt::MoveAction); event->accept(); }
    else event->ignore();
}
void PlotLayoutWidget::titleMenu(QUuid cell, QPoint position)
{
    if (!m_panels.value(cell)) return;
    QMenu menu;
    QMap<QAction *, PlotPlacement> moves;
    const QVector<QPair<const char *, PlotPlacement>> choices = {
        {"ui.plot_move_page_top", PlotPlacement::Above}, {"ui.plot_move_page_bottom", PlotPlacement::Below},
        {"ui.plot_move_page_left", PlotPlacement::Left}, {"ui.plot_move_page_right", PlotPlacement::Right}};
    for (const auto &choice : choices)
    {
        auto *action = menu.addAction(text(choice.first));
        action->setEnabled(m_panels.size() > 1); moves[action] = choice.second;
    }
    auto *swap = menu.addMenu(text("ui.plot_swap_with"));
    QMap<QAction *, QUuid> swaps;
    int index = 0;
    for (auto id : m_layout.order())
    {
        ++index;
        if (id == cell || !m_panels.value(id)) continue;
        auto *button = m_panels[id]->findChild<QPushButton *>("selectSubplot");
        swaps[swap->addAction(QString("%1 · %2").arg(index).arg(button ? button->text() : QString()))] = id;
    }
    swap->setEnabled(!swaps.isEmpty());
    menu.addSeparator();
    auto *reset = menu.addAction(text("ui.plot_reset_arrangement"));
    QPointer<PlotLayoutWidget> guard(this);
    auto *selected = menu.exec(position);
    if (!guard || !selected) return;
    if (moves.contains(selected)) moveCell(cell, {}, moves[selected], true);
    else if (swaps.contains(selected)) moveCell(cell, swaps[selected], PlotPlacement::Swap);
    else if (selected == reset) resetArrangement();
}
}
