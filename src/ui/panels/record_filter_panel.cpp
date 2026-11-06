// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
#include "ui/panels/record_filter_panel.h"
#include "ui/common/presentation.h"
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QSignalBlocker>
#include <QDate>
#include <limits>
namespace aster::ui
{
RecordFilterPanel::RecordFilterPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_text = new QLineEdit;
    m_text->setObjectName("recordSearch");
    m_text->setPlaceholderText(text("ui.name_path_notes"));
    layout->addWidget(m_text);
    auto *toggle = new QPushButton(text("ui.filters"));
    toggle->setCheckable(true);
    layout->addWidget(toggle);
    auto *panel = new QWidget;
    layout->addWidget(panel);
    panel->hide();
    connect(toggle, &QPushButton::toggled, panel, &QWidget::setVisible);
    auto *form = new QFormLayout(panel);
    form->setContentsMargins(0, 0, 0, 0);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_date = new ChoiceEditor;
    m_date->addItems(
        {text("ui.all_time"), text("ui.today"), text("ui.last_7_days")});
    m_min = new QLineEdit;
    m_min->setObjectName("minimumFrequency");
    m_min->setPlaceholderText(text("ui.no_limit"));
    m_max = new QLineEdit;
    m_max->setObjectName("maximumFrequency");
    m_max->setPlaceholderText(text("ui.no_limit"));
    m_unit = new ChoiceEditor;
    m_unit->setObjectName("frequencyUnit");
    setUnits({});
    for (auto *combo : {m_date, m_unit})
    {
        combo->setMinimumContentsLength(5);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    form->addRow(text("ui.date"), m_date);
    form->addRow(text("ui.min_frequency"), m_min);
    form->addRow(text("ui.max_frequency"), m_max);
    form->addRow(text("ui.unit"), m_unit);
    auto *reset = new QPushButton(text("ui.reset_filters"));
    form->addRow(reset);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(250);
    for (auto *input : {m_text, m_min, m_max})
        connect(input, &QLineEdit::textChanged, this, [this, timer] { emit editing(); timer->start(); });
    for (auto *combo : {m_date, m_unit})
        connect(combo, qOverload<int>(&QComboBox::activated), this, [this, timer] { emit editing(); timer->start(); });
    connect(timer, &QTimer::timeout, this, &RecordFilterPanel::changed);
    connect(reset, &QPushButton::clicked, this,
            [this, timer]
            {
                emit editing();
                m_text->clear();
                m_min->clear();
                m_max->clear();
                m_date->setCurrentIndex(0);
                m_unit->setCurrentIndex(0);
                timer->start();
            });
}
RecordFilter RecordFilterPanel::filter() const
{
    RecordFilter f;
    f.text = m_text->text().trimmed();
    if (m_date->currentIndex() == 1)
        f.since = QDate::currentDate().startOfDay();
    if (m_date->currentIndex() == 2)
        f.since = QDateTime::currentDateTime().addDays(-7);
    const auto number = [](QLineEdit *edit)
    {
        if (edit->text().trimmed().isEmpty())
            return -1.0;
        bool ok;
        const double value = edit->text().toDouble(&ok);
        return ok && value >= 0 ? value : std::numeric_limits<double>::quiet_NaN();
    };
    f.minFrequency = number(m_min);
    f.maxFrequency = number(m_max);
    f.frequencyUnit = m_unit->currentData().toString();
    return f;
}
void RecordFilterPanel::setUnits(const QStringList &units)
{
    QSignalBlocker block(m_unit);
    const auto selected = m_unit->currentData().toString();
    m_unit->clear();
    m_unit->addItem(text("ui.unknown_unit_only"), QString(""));
    for (const auto &unit : units)
        if (!unit.isEmpty())
            m_unit->addItem(unit, unit);
    if (!selected.isEmpty() && m_unit->findData(selected) < 0)
        m_unit->addItem(selected, selected);
    m_unit->setCurrentIndex(qMax(0, m_unit->findData(selected)));
}
} // namespace aster::ui
