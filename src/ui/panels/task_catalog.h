#ifndef ASTER_UI_TASK_CATALOG_H
#define ASTER_UI_TASK_CATALOG_H
#include "core/task_registry.h"
#include <QWidget>
class QTreeWidget;
class QLineEdit;
class QMenu;
namespace aster::ui
{
class TaskCatalog : public QWidget
{
    Q_OBJECT
  public:
    explicit TaskCatalog(const TaskRegistry &registry, QWidget *parent = nullptr);
    QMenu *menu(QWidget *owner, const std::function<bool(const TaskDescriptor &)> &allowed,
                std::function<void(QString)> chosen) const;
  signals:
    void methodActivated(QString key);

  private:
    TaskRegistry m_registry;
    QTreeWidget *m_tree;
    QLineEdit *m_search;
};
} // namespace aster::ui
#endif
