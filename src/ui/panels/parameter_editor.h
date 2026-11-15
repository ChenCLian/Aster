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
class QPushButton;
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
    void setScope(const QString &label, bool paired, bool editable, bool canApplyAll);
    void setError(const QString &error);
  signals:
    void applyRequested();
    void applyAllRequested();
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
    QLabel *m_scopeLabel;
    QPushButton *m_applyAll;
    QWidget *m_scopeFooter;
    QTimer m_commit;
    QMap<QString, QWidget *> m_fields;
    bool m_dirty = false;
    QJsonObject m_hiddenParameters;
};
} // namespace aster::ui
#endif
