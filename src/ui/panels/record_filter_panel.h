#ifndef ASTER_RECORD_FILTER_PANEL_H
#define ASTER_RECORD_FILTER_PANEL_H
#include "io/database/record_filter.h"
#include <QWidget>
class QLineEdit;
class QComboBox;
namespace aster::ui
{
class RecordFilterPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit RecordFilterPanel(QWidget *parent = nullptr);
    RecordFilter filter() const;
    void setUnits(const QStringList &units);
  signals:
    void changed();
    void editing();

  private:
    QLineEdit *m_text, *m_min, *m_max;
    QComboBox *m_date, *m_unit;
};
} // namespace aster::ui
#endif
