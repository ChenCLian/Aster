// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_RECORD_FILTER_PANEL_H
#define ASTER_RECORD_FILTER_PANEL_H
#include "io/database/record_filter.h"
#include <QWidget>
class QLineEdit;
class QComboBox;
class QLabel;
class QToolButton;
namespace aster::ui
{
class RecordFilterPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit RecordFilterPanel(QWidget *parent = nullptr);
    RecordFilter filter() const;
    // True when the active peak range needs to be queried with a different unit.
    bool setUnits(const QStringList &units);
  signals:
    void changed();
    void editing();
    void idSubmitted(qint64 id);
  private:
    void updateSummary();
    void updateUnitLabels();
    QLineEdit *m_text, *m_min, *m_max;
    QComboBox *m_date, *m_axis, *m_unit;
    QToolButton *m_dateChip, *m_peakChip, *m_filterButton;
    QWidget *m_chips;
    QLabel *m_error, *m_unitText;
    bool m_period = true;
};
} // namespace aster::ui
#endif
