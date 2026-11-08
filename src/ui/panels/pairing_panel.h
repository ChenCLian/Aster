// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PAIRING_PANEL_H
#define ASTER_PAIRING_PANEL_H
#include "ui/workflow/workflow_document.h"
#include <QWidget>
class QComboBox;
namespace aster::ui
{
class PairingPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit PairingPanel(WorkflowDocument *, QWidget *parent = nullptr);
    void selectNode(NodeId);
    void refresh();
  signals:
    void pairSelected(QUuid);

  private:
    void editPairs();
    WorkflowDocument *m_document;
    NodeId m_node;
    QComboBox *m_pairs;
};
} // namespace aster::ui
#endif
