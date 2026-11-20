// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/panels/record_filter_panel.h"
#include "ui/common/choice_editor.h"
#include "ui/common/popup_placement.h"
#include "ui/common/presentation.h"
#include "model/frequency_units.h"
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace aster::ui
{
namespace
{
double limitValue(const QLineEdit *edit)
{
    const auto raw = edit->text().trimmed();
    if (raw.isEmpty()) return -1;
    bool ok = false;
    const auto value = raw.toDouble(&ok);
    return ok && std::isfinite(value) && value >= 0 ? value : std::numeric_limits<double>::quiet_NaN();
}
QString unitLabel(const QString &unit, bool period)
{
    if (unit.isEmpty()) return text("ui.undeclared_peak_unit");
    if (!period) return unit;
    const auto derived = periodUnitForFrequency(unit);
    return derived.isEmpty() ? "1/(" + unit + ")" : derived;
}
}
RecordFilterPanel::RecordFilterPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *searchRow = new QHBoxLayout;
    m_text = new QLineEdit;
    m_text->setObjectName("recordSearch");
    m_text->setPlaceholderText(text("ui.name_path_notes"));
    m_text->setAccessibleName(text("ui.name_path_notes"));
    m_text->setAccessibleDescription(text("ui.record_search_help"));
    m_text->setToolTip(text("ui.record_search_help"));
    m_text->setClearButtonEnabled(true);
    searchRow->addWidget(m_text, 1);
    m_filterButton = new QToolButton;
    m_filterButton->setObjectName("recordFilterButton");
    m_filterButton->setText(text("ui.record_filters"));
    searchRow->addWidget(m_filterButton);
    layout->addLayout(searchRow);
    m_chips = new QWidget;
    auto *chips = new QHBoxLayout(m_chips);
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(4);
    m_dateChip = new QToolButton;
    m_peakChip = new QToolButton;
    for (auto *chip : {m_dateChip, m_peakChip}) {
        chip->setProperty("filterChip", true);
        chip->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        chip->setMinimumWidth(0);
        chips->addWidget(chip);
    }
    chips->addStretch();
    layout->addWidget(m_chips);
    auto *panel = new QDialog(this, Qt::Popup);
    panel->setObjectName("recordFilterPopup");
    panel->setWindowTitle(text("ui.record_filters"));
    auto *popupLayout = new QVBoxLayout(panel);
    popupLayout->setContentsMargins(12, 10, 12, 8);
    auto *body = new QWidget;
    auto *form = new QFormLayout(body);
    form->setContentsMargins(0, 0, 0, 0);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *title = new QLabel(text("ui.record_filters"));
    title->setObjectName("SectionTitle");
    form->addRow(title);
    m_date = new ChoiceEditor;
    m_date->addItems({text("ui.all_time"), text("ui.today"), text("ui.last_7_days")});
    m_axis = new ChoiceEditor;
    m_axis->setObjectName("recordPeakAxis");
    m_axis->addItems({text("ui.peak_frequency"), text("ui.peak_period")});
    m_axis->setCurrentIndex(1);
    m_min = new QLineEdit;
    m_min->setObjectName("minimumFrequency");
    m_max = new QLineEdit;
    m_max->setObjectName("maximumFrequency");
    auto *range = new QHBoxLayout;
    for (auto *edit : {m_min, m_max}) {
        edit->setMinimumWidth(35);
        const auto label = text(edit == m_min ? "ui.lower_bound" : "ui.upper_bound");
        edit->setPlaceholderText(label);
        edit->setAccessibleName(label);
        range->addWidget(edit, 1);
        if (edit == m_min) range->addWidget(new QLabel(QStringLiteral("–")));
    }
    m_unit = new ChoiceEditor;
    m_unit->setObjectName("frequencyUnit");
    for (auto *combo : {m_date, m_axis, m_unit}) {
        combo->setMinimumContentsLength(5);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    m_unit->setAccessibleName(text("ui.peak_bounds_unit"));
    m_unit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_unit->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_unit->setMaximumWidth(120);
    m_unitText = new QLabel;
    m_unitText->setTextFormat(Qt::PlainText);
    m_unitText->setObjectName("peakRangeUnit");
    m_unitText->setMaximumWidth(120);
    m_unitText->hide();
    m_unit->hide();
    range->addWidget(m_unitText);
    range->addWidget(m_unit);
    form->addRow(text("ui.date"), m_date);
    form->addRow(text("ui.peak_bounds_axis"), m_axis);
    form->addRow(text("ui.peak_bounds_range"), range);
    m_error = new QLabel;
    m_error->setObjectName("filterValidation");
    m_error->setWordWrap(true);
    form->addRow(m_error);
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(body);
    popupLayout->addWidget(scroll, 1);
    auto *actions = new QHBoxLayout;
    auto *reset = new QPushButton(text("ui.reset_filters"));
    auto *done = new QPushButton(text("ui.filter_done"));
    reset->setAutoDefault(false);
    actions->addWidget(reset);
    actions->addStretch();
    actions->addWidget(done);
    popupLayout->addLayout(actions);
    connect(done, &QPushButton::clicked, panel, &QDialog::accept);
    connect(m_filterButton, &QToolButton::clicked, this, [this, panel, body] {
        panel->ensurePolished();
        const QRect anchor(m_filterButton->mapToGlobal(QPoint()), m_filterButton->size());
        const auto size = QSize(370, body->sizeHint().height() + 76);
        panel->setGeometry(popupGeometry(size, anchor, popupBounds(this, anchor)));
        panel->show();
        m_axis->setFocus();
    });
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(250);
    connect(m_text, &QLineEdit::returnPressed, this, [this, timer] {
        if (const auto id = filter().exactRecordId()) {
            timer->stop();
            emit idSubmitted(*id);
        }
    });
    const auto edit = [this, timer] { updateSummary(); emit editing(); timer->start(); };
    for (auto *field : {m_text, m_min, m_max}) connect(field, &QLineEdit::textChanged, this, edit);
    for (auto *combo : {m_date, m_unit}) connect(combo, &QComboBox::currentIndexChanged, this, edit);
    connect(m_axis, &QComboBox::currentIndexChanged, this, [this, edit](int index) {
        // Preserve the selected physical interval, including a single open end.
        const auto lower = limitValue(m_min), upper = limitValue(m_max);
        const auto convert = [](double value) {
            return value <= 0 ? QString() : QString::number(positiveReciprocal(value), 'g', 17);
        };
        if (!filter().validate().isEmpty() || (!m_period && upper == 0) ||
            (lower > 0 && !std::isfinite(positiveReciprocal(lower))) ||
            (upper > 0 && !std::isfinite(positiveReciprocal(upper)))) {
            QSignalBlocker block(m_axis);
            m_axis->setCurrentIndex(m_period ? 1 : 0);
            m_error->setText(text("ui.range_conversion_error")); m_error->show();
            return;
        }
        m_period = index == 1;
        { QSignalBlocker a(m_min), b(m_max);
          m_min->setText(convert(upper)); m_max->setText(convert(lower)); }
        updateUnitLabels();
        edit();
    });
    connect(timer, &QTimer::timeout, this, &RecordFilterPanel::changed);
    connect(m_dateChip, &QToolButton::clicked, this, [this] { m_date->setCurrentIndex(0); });
    connect(m_peakChip, &QToolButton::clicked, this, [this] { m_min->clear(); m_max->clear(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_text->clear(); m_min->clear(); m_max->clear(); m_date->setCurrentIndex(0);
        m_unit->setCurrentIndex(0); m_axis->setCurrentIndex(1); updateSummary();
    });
    updateSummary();
}
RecordFilter RecordFilterPanel::filter() const
{
    RecordFilter f;
    f.text = m_text->text().trimmed();
    if (f.exactRecordId()) return f;
    if (m_date->currentIndex() == 1) f.since = QDate::currentDate().startOfDay();
    if (m_date->currentIndex() == 2) f.since = QDateTime::currentDateTime().addDays(-7);
    const double low = limitValue(m_min), high = limitValue(m_max);
    f.minFrequency = m_period && high != -1 ? positiveReciprocal(high) : (m_period ? -1 : low);
    f.maxFrequency = m_period && low != -1 ? positiveReciprocal(low) : (m_period ? -1 : high);
    f.frequencyUnit = m_unit->currentData().toString();
    return f;
}
void RecordFilterPanel::updateSummary()
{
    const auto f = filter();
    const bool byId = f.exactRecordId().has_value();
    m_unit->setToolTip(m_unit->currentText());
    m_filterButton->setEnabled(!byId);
    const bool date = !byId && m_date->currentIndex() != 0;
    const bool peak = !byId && (!m_min->text().trimmed().isEmpty() || !m_max->text().trimmed().isEmpty());
    const bool invalid = !f.validate().isEmpty();
    m_error->setText(text(m_period ? "ui.invalid_period_range" : "ui.invalid_frequency_range"));
    m_error->setVisible(invalid);
    const auto chip = [](QToolButton *button, const QString &caption) {
        button->setText(caption + QStringLiteral(" ×"));
        button->setToolTip(text("ui.remove_filter").arg(caption));
        button->setAccessibleName(button->toolTip());
    };
    chip(m_dateChip, m_date->currentText());
    const auto number = [](QLineEdit *field) {
        bool ok = false;
        const auto n = field->text().toDouble(&ok);
        return ok ? QString::number(n, 'g', 5) : field->text();
    };
    auto range = number(m_min) + QStringLiteral("–") + number(m_max);
    const auto unit = m_unit->currentData().toString();
    if (!unit.isEmpty()) range += " " + unitLabel(unit, m_period);
    chip(m_peakChip, m_axis->currentText() + " " + range);
    m_dateChip->setVisible(date); m_peakChip->setVisible(peak); m_chips->setVisible(date || peak);
}
void RecordFilterPanel::updateUnitLabels()
{
    const QSignalBlocker block(m_unit);
    for (int i = 0; i < m_unit->count(); ++i)
        m_unit->setItemText(i, unitLabel(m_unit->itemData(i).toString(), m_period));
    m_unit->setVisible(m_unit->count() > 1);
    const bool single = m_unit->count() == 1 && !m_unit->currentData().toString().isEmpty();
    const auto label = m_unit->currentText();
    m_unitText->setText(m_unitText->fontMetrics().elidedText(label, Qt::ElideMiddle, m_unitText->maximumWidth()));
    m_unitText->setToolTip(label);
    m_unitText->setVisible(single);
}

bool RecordFilterPanel::setUnits(const QStringList &units)
{
    QStringList previous;
    for (int i = 0; i < m_unit->count(); ++i)
        previous.append(m_unit->itemData(i).toString());
    if (previous == units) return false;

    const auto selected = m_unit->currentData().toString();
    const QSignalBlocker block(m_unit);
    m_unit->clear();
    for (const auto &unit : units)
        m_unit->addItem(unitLabel(unit, m_period), unit);
    const int index = units.indexOf(selected);
    if (index >= 0) m_unit->setCurrentIndex(index);
    updateUnitLabels();
    updateSummary();
    return selected != m_unit->currentData().toString() && filter().hasFrequency();
}
} // namespace aster::ui
