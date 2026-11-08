// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_NODE_EDIT_DIALOG_H
#define ASTER_UI_NODE_EDIT_DIALOG_H
#include "ui/workflow/workflow_document.h"
#include <QDialog>
#include <QPointer>
class QComboBox;
class QLabel;
class QPushButton;
namespace aster::ui {
QString nodeRewriteMessage(const QString &error);
// A review form. Only its confirm action commits; opening/choosing/cancelling
// never changes the graph. Any intervening document edit closes the form.
class NodeEditDialog final : public QDialog {
    Q_OBJECT
public:
    NodeEditDialog(WorkflowDocument *document, NodeRewriteKind kind, NodeId target,
                   QString method, QWidget *parent);
    void openAt(const QRect &anchor);
signals:
    void committed(aster::NodeId id);
private:
    void updateCandidate();
    QPointer<WorkflowDocument> m_document;
    NodeRewriteRequest m_request;
    PreparedNodeRewrite m_prepared;
    quint64 m_revision;
    bool m_committing = false;
    QComboBox *m_input = nullptr;
    QMap<NodeId, QComboBox *> m_outputs;
    QLabel *m_status = nullptr;
    QPushButton *m_confirm = nullptr;
};
}
#endif
