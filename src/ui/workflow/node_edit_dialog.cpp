// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/workflow/node_edit_dialog.h"
#include "ui/common/presentation.h"
#include "ui/common/popup_placement.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

namespace aster::ui {
QString nodeRewriteMessage(const QString &error)
{
    if (error.isEmpty()) return {};
    const auto key = "ui." + error.toLower();
    const auto translated = text(key.toUtf8().constData());
    return translated == key ? error : translated;
}
NodeEditDialog::NodeEditDialog(WorkflowDocument *doc, NodeRewriteKind kind, NodeId target,
                               QString method, QWidget *parent)
    : QDialog(parent), m_document(doc), m_revision(doc->revision())
{
    setObjectName("nodeEditDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);
    const bool replacing = kind == NodeRewriteKind::Replace;
    setWindowTitle(text(replacing ? "ui.replace_node" : "ui.insert_node"));
    m_request.kind = kind; m_request.target = target; m_request.method = std::move(method);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 16); layout->setSpacing(14);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body); bodyLayout->setContentsMargins(0, 0, 8, 0);
    bodyLayout->setSpacing(14);
    const auto options = inspectNodeRewrite(doc->model(), kind, target, m_request.method);
    const auto *node = doc->node(target);
    const auto *entry = doc->model().registry().find(m_request.method);
    auto *title = new QLabel;
    title->setTextFormat(Qt::PlainText); title->setWordWrap(true);
    auto titleFont = title->font(); titleFont.setBold(true); titleFont.setPointSizeF(12); title->setFont(titleFont);
    if (node && entry) {
        const auto oldName = methodLabel(doc->model().registry().find(node->method)->descriptor);
        title->setText(text(replacing ? "ui.replace_node_summary" : "ui.insert_node_summary")
                       .arg(oldName, methodLabel(entry->descriptor)));
    }
    bodyLayout->addWidget(title);
    auto *note = new QLabel(text("ui.node_edit_defaults")); note->setWordWrap(true);
    bodyLayout->addWidget(note);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows); form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto choice = [this](const QVector<OutputSpec> &ports, const QString &previous,
                                const TaskDescriptor &producer, const QString &name) {
        auto *box = new QComboBox; box->setObjectName(name);
        box->setMinimumWidth(0); box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(12);
        box->addItem(text("ui.node_edit_choose_output"), QString());
        for (const auto &port : ports) {
            const auto label = portLabel(producer, port.key);
            box->addItem(label, port.key); box->setItemData(box->count() - 1, label, Qt::ToolTipRole);
        }
        const int old = box->findData(previous);
        box->setCurrentIndex(old > 0 ? old : ports.size() == 1 ? 1 : 0);
        connect(box, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateCandidate(); });
        return box;
    };
    if (options.ok()) {
        const auto &upstream = doc->model().registry().find(doc->node(options.parent)->method)->descriptor;
        m_input = choice(options.inputs, options.previousInput, upstream, "nodeEditInput");
        auto *inputLabel = new QLabel(text("ui.node_edit_input_from").arg(methodLabel(upstream)));
        inputLabel->setWordWrap(true); form->addRow(inputLabel, m_input);
        for (const auto &mapping : options.outputs) {
            const auto &downstream = doc->model().registry().find(doc->node(mapping.child)->method)->descriptor;
            auto *box = choice(mapping.choices, mapping.previousOutput, entry->descriptor,
                               "nodeEditOutput_" + mapping.child.toString(QUuid::WithoutBraces));
            m_outputs.insert(mapping.child, box);
            const auto slot = doc->node(mapping.child)->input->slot;
            const auto branch = text(slot == ChildSlot::Left ? "ui.node_edit_left" :
                                     slot == ChildSlot::Right ? "ui.node_edit_right" : "ui.node_edit_down");
            const auto destination = kind == NodeRewriteKind::Replace
                ? methodLabel(downstream) + " · " + branch : methodLabel(downstream);
            auto *label = new QLabel(text("ui.node_edit_output_to").arg(destination));
            label->setWordWrap(true); form->addRow(label, box);
        }
    }
    bodyLayout->addLayout(form); bodyLayout->addStretch();
    scroll->setWidget(body); layout->addWidget(scroll, 1);
    m_status = new QLabel; m_status->setObjectName("nodeEditStatus"); m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_confirm = buttons->button(QDialogButtonBox::Ok);
    m_confirm->setObjectName("nodeEditConfirm");
    m_confirm->setText(text(replacing ? "ui.replace_node" : "ui.insert_node"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!m_document || m_document->revision() != m_revision || !m_prepared.ok()) { reject(); return; }
        m_committing = true;
        const auto committedEdit = m_document->commitEdit(m_prepared.edit);
        m_committing = false;
        if (!committedEdit.ok()) { m_status->setText(nodeRewriteMessage(committedEdit.error)); m_confirm->setEnabled(false); return; }
        const auto id = m_prepared.nodeId;
        accept(); emit committed(id);
    });
    layout->addWidget(buttons);
    const auto invalidate = [this] { if (!m_committing) reject(); };
    connect(doc, &WorkflowDocument::changed, this, invalidate);
    connect(doc, &WorkflowDocument::sourcesChanged, this, invalidate);
    connect(doc, &QObject::destroyed, this, &QDialog::reject);
    if (options.ok()) updateCandidate();
    else { m_status->setText(nodeRewriteMessage(options.error)); m_confirm->setEnabled(false); }
}
void NodeEditDialog::updateCandidate()
{
    if (!m_input || !m_confirm) return;
    if (!m_document || m_document->revision() != m_revision) { reject(); return; }
    m_request.inputOutput = m_input->currentData().toString();
    m_request.childOutputs.clear();
    for (auto it = m_outputs.cbegin(); it != m_outputs.cend(); ++it)
        m_request.childOutputs[it.key()] = it.value()->currentData().toString();
    m_prepared = m_document->prepareRewrite(m_request);
    m_confirm->setEnabled(m_prepared.ok());
    m_status->setText(m_prepared.ok() ? text("ui.node_edit_ready") : nodeRewriteMessage(m_prepared.error));
}
void NodeEditDialog::openAt(const QRect &anchor)
{
    if (!m_document || m_document->revision() != m_revision || m_document->editingLocked()) { reject(); return; }
    ensurePolished();
    const auto bounds = popupBounds(parentWidget(), anchor);
    // A review form stays within its owner's current screen; long labels scroll.
    setMinimumSize(std::min(380, bounds.width()), std::min(260, bounds.height()));
    setMaximumSize(bounds.size());
    setGeometry(popupGeometry(QSize(560, 460), anchor, bounds));
    show(); raise(); activateWindow();
}
}
