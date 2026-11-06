// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_PARAMETER_EDITOR_H
#define ASTER_UI_PARAMETER_EDITOR_H
#include "core/task_registry.h"
#include <QWidget>
#include <QTimer>
class QVBoxLayout;
class QLabel;
class QToolButton;
namespace aster::ui
{
class ParameterEditor : public QWidget
{
    Q_OBJECT
  public:
    explicit ParameterEditor(QWidget *parent = nullptr);
    ~ParameterEditor() override;
    void showNode(const NodeDefinition *node, const TaskRegistry &registry);
    NodeId nodeId() const
    {
        return m_id;
    }
    QJsonObject parameters() const;
    bool dirty() const
    {
        return m_dirty;
    }
    void markClean();
    void setError(const QString &error);
  signals:
    void applyRequested();
    void edited();

  private:
    NodeId m_id;
    QString m_method;
    QVBoxLayout *m_form;
    QMap<QString, QWidget *> m_rows;
    QVector<ParameterSpec> m_specs;
    void updateVisibility();
    QLabel *m_title;
    QToolButton *m_help;
    QLabel *m_description;
    QLabel *m_error;
    QTimer m_commit;
    QMap<QString, QWidget *> m_fields;
    bool m_dirty = false;
};
} // namespace aster::ui
#endif
