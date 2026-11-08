// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_JOINT_INPUT_DIALOG_H
#define ASTER_UI_JOINT_INPUT_DIALOG_H
#include "ui/workflow/workflow_document.h"
#include <QDialog>
#include <QPointer>
#include <memory>
namespace aster::ui {
class JointInputCatalog;
QString jointOriginLabel(const WorkflowDocument&, NodeId);
QString jointMemberLabel(const WorkflowDocument&, NodeId joint, const BatchSeriesRef&);
bool jointMemberAvailable(const WorkflowDocument&, NodeId joint, const BatchSeriesRef&);
// Only explicit confirmation returns a value. Any document edit invalidates the chooser.
class JointInputDialog final : public QDialog {
    Q_OBJECT
public:
    JointInputDialog(WorkflowDocument*, NodeId joint, BatchSeriesRef current, QWidget* parent);
    ~JointInputDialog() override;
    void openWithinOwner();
protected:
    void resizeEvent(QResizeEvent*) override;
signals:
    void chosen(QUuid node, QUuid dataset, QString port);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
class PairingDialog final : public QDialog {
    Q_OBJECT
public:
    PairingDialog(WorkflowDocument*, NodeId joint, QWidget* parent);
    ~PairingDialog() override;
    void openWithinOwner();
protected:
    void resizeEvent(QResizeEvent*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
#endif
