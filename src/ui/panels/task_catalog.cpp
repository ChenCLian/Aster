// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/panels/task_catalog.h"
#include "ui/common/task_drag_preview.h"
#include <QMouseEvent>
#include <QLabel>
#include "ui/common/presentation.h"
#include "ui/settings/theme_manager.h"
#include "ui/workflow/workflow_canvas.h"
#include <QTreeWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QDrag>
#include <QPointer>
#include <QMimeData>
#include <QMenu>
#include <QApplication>
#include <algorithm>
namespace aster::ui
{
namespace
{
QVector<TaskDescriptor> orderedTasks(const TaskRegistry &registry)
{
    auto tasks = registry.descriptors();
    const auto rank = [](const TaskDescriptor &d)
    {
        return !d.inputKind ? 0 : d.category.value(0) == "Preprocess" ? 1 : 2;
    };
    std::stable_sort(tasks.begin(), tasks.end(),
                     [&](const TaskDescriptor &a, const TaskDescriptor &b)
                     {
                         return rank(a) != rank(b) ? rank(a) < rank(b)
                                                   : a.category.join('/') < b.category.join('/');
                     });
    return tasks;
}
class TaskTree : public QTreeWidget
{
  public:
    explicit TaskTree(const TaskRegistry &registry) : m_registry(registry)
    {
        setExpandsOnDoubleClick(false);
    }

  private:
    TaskRegistry m_registry;
    bool categoryAt(QMouseEvent *event)
    {
        auto *item = itemAt(event->position().toPoint());
        return item && item->data(0, Qt::UserRole).toString().isEmpty();
    }

  protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && categoryAt(event))
        {
            auto *item = itemAt(event->position().toPoint());
            setCurrentItem(item);
            item->setExpanded(!item->isExpanded());
            event->accept();
            return;
        }
        QTreeWidget::mousePressEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && categoryAt(event))
        {
            event->accept();
            return;
        }
        QTreeWidget::mouseDoubleClickEvent(event);
    }
    void startDrag(Qt::DropActions) override
    {
        auto *item = currentItem();
        if (!item)
            return;
        auto key = item->data(0, Qt::UserRole).toString();
        const auto *entry = m_registry.find(key);
        if (!entry || !entry->descriptor.catalogVisible)
            return;
        QPointer<QDrag> drag = new QDrag(this);
        auto *mime = new QMimeData;
        mime->setData(TaskMimeType, key.toUtf8());
        drag->setMimeData(mime);
        const auto preview = taskDragPreview(entry->descriptor, font(), devicePixelRatioF());
        drag->setPixmap(preview.pixmap);
        drag->setHotSpot(preview.hotSpot);
        drag->exec(Qt::CopyAction);
        if (drag)
            drag->deleteLater();
    }
};
bool showMatching(QTreeWidgetItem *item, const QString &query)
{
    bool match = item->text(0).contains(query, Qt::CaseInsensitive) ||
                 item->data(0, Qt::UserRole).toString().contains(query, Qt::CaseInsensitive);
    bool child = false;
    for (int i = 0; i < item->childCount(); ++i)
        child = showMatching(item->child(i), match ? QString() : query) || child;
    item->setHidden(!match && !child);
    return match || child;
}
} // namespace
TaskCatalog::TaskCatalog(const TaskRegistry &r, QWidget *p) : QWidget(p), m_registry(r)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(text("ui.filter_tasks_or_browse_categories"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);
    m_tree = new TaskTree(r);
    m_tree->setObjectName("taskCatalog");
    m_tree->setHeaderHidden(true);
    m_tree->setDragEnabled(true);
    m_tree->setIndentation(14);
    m_tree->setMinimumWidth(0);
    layout->addWidget(m_tree);
    QMap<QString, QTreeWidgetItem *> categories;
    for (const auto &d : orderedTasks(r))
    {
        if (!d.catalogVisible)
            continue;
        QTreeWidgetItem *parent = nullptr;
        QString path;
        for (const auto &category : d.category)
        {
            path += "/" + category;
            if (!categories.contains(path))
            {
                auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
                item->setText(0, categoryLabel(category));
                item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
                categories.insert(path, item);
            }
            parent = categories[path];
        }
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, methodLabel(d));
        item->setIcon(0, methodIcon(!d.inputKind));
        item->setData(0, Qt::UserRole, d.key);
        item->setToolTip(0, methodLabel(d) + "\n" + text("ui.catalog_interaction_hint"));
    }
    m_tree->expandToDepth(0);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this,
            [this]
            {
                QTreeWidgetItemIterator it(m_tree);
                for (; *it; ++it)
                    if (const auto *entry =
                            m_registry.find((*it)->data(0, Qt::UserRole).toString()))
                        (*it)->setIcon(0, methodIcon(!entry->descriptor.inputKind));
            });
    connect(m_search, &QLineEdit::textChanged, this,
            [this](const QString &q)
            {
                for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
                    showMatching(m_tree->topLevelItem(i), q);
                if (!q.isEmpty())
                    m_tree->expandAll();
            });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item)
            {
                auto key = item->data(0, Qt::UserRole).toString();
                if (!key.isEmpty())
                    emit methodActivated(key);
            });
}
QMenu *TaskCatalog::menu(QWidget *owner, const std::function<bool(const TaskDescriptor &)> &allowed,
                         std::function<void(QString)> chosen) const
{
    auto configure = [](QMenu *menu)
    {
        menu->setObjectName("taskPicker");
        menu->setMinimumWidth(240);
        auto font = QApplication::font();
        font.setPointSizeF(std::max(11.0, font.pointSizeF()));
        menu->setFont(font);
        menu->setToolTipsVisible(true);
    };
    auto *root = new QMenu(owner);
    configure(root);
    root->setAttribute(Qt::WA_DeleteOnClose);
    QMap<QString, QMenu *> categories;
    for (const auto &d : orderedTasks(m_registry))
    {
        if (!d.catalogVisible || !allowed(d))
            continue;
        QMenu *parent = root;
        QString path;
        for (const auto &category : d.category)
        {
            path += "/" + category;
            if (!categories.contains(path))
            {
                auto *menu = parent->addMenu(categoryLabel(category));
                configure(menu);
                categories.insert(path, menu);
            }
            parent = categories[path];
        }
        auto *action = parent->addAction(methodIcon(!d.inputKind), methodLabel(d));
        action->setData(d.key);
        QObject::connect(action, &QAction::triggered, root, [chosen, key = d.key] { chosen(key); });
    }
    if (root->actions().isEmpty())
    {
        auto *a = root->addAction(text("ui.no_compatible_task_here"));
        a->setEnabled(false);
    }
    return root;
}
} // namespace aster::ui
