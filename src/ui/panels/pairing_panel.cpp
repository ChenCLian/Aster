// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/panels/pairing_panel.h"
#include "ui/panels/joint_input_dialog.h"
#include "ui/common/choice_editor.h"
#include "ui/common/presentation.h"
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace aster::ui
{
PairingPanel::PairingPanel(WorkflowDocument *doc, QWidget *parent)
    : QWidget(parent), m_document(doc)
{
    setObjectName("pairingPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 5, 8);
    auto *label = new QLabel(text("ui.pair_preview"));
    label->setObjectName("SectionTitle");
    layout->addWidget(label);
    m_pairs = new ChoiceEditor;
    m_pairs->setObjectName("previewPair");
    m_pairs->setMinimumWidth(0);
    m_pairs->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_pairs->setMinimumContentsLength(10);
    layout->addWidget(m_pairs);
    auto *button = new QPushButton(text("ui.manage_pairs"));
    button->setObjectName("managePairs");
    layout->addWidget(button);
    connect(button, &QPushButton::clicked, this, &PairingPanel::editPairs);
    connect(m_pairs, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { emit pairSelected(m_pairs->currentData().toUuid()); });
    connect(doc, &WorkflowDocument::sourcesChanged, this, &PairingPanel::refresh);
    connect(doc, &WorkflowDocument::changed, this, &PairingPanel::refresh);
    hide();
}
void PairingPanel::selectNode(NodeId id)
{
    m_node = m_document->pairNodeFor(id);
    refresh();
}
void PairingPanel::refresh()
{
    const auto *node = m_document->node(m_node);
    const bool paired =
        node && m_document->model().registry().find(node->method)->descriptor.pairedInput;
    setVisible(paired);
    auto *manage = findChild<QPushButton *>("managePairs");
    manage->setEnabled(paired && !m_document->editingLocked());
    manage->setToolTip(text("ui.joint_manage_hint"));
    const auto previous = m_pairs->currentData().toUuid();
    {
        QSignalBlocker blocker(m_pairs);
        m_pairs->clear();
        if (paired)
            for (const auto &pair : m_document->grouping().value(m_node))
                m_pairs->addItem(pair.label.isEmpty()
                                     ? text("ui.pair_number").arg(m_pairs->count() + 1)
                                     : pair.label,
                                 pair.id);
        const auto index = m_pairs->findData(previous);
        if (index >= 0)
            m_pairs->setCurrentIndex(index);
    }
    if (paired)
        emit pairSelected(m_pairs->currentData().toUuid());
}
void PairingPanel::editPairs()
{
    if (m_document->editingLocked() || !m_document->node(m_node)) return;
    for (auto* existing : findChildren<PairingDialog*>())
        if (existing->isVisible()) { existing->raise(); existing->activateWindow(); return; }
    auto* dialog = new PairingDialog(m_document, m_node, this);
    dialog->openWithinOwner();
}
} // namespace aster::ui
