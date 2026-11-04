#ifndef ASTER_UI_PRESENTATION_H
#define ASTER_UI_PRESENTATION_H
#include "core/task_registry.h"
#include <QColor>
#include <QIcon>
class QApplication;
namespace aster::ui
{
void setLanguage(const QString &language);
QString text(const char *key);
QString translatedLabel(const QString &key, const QString &fallback);
QString methodLabel(const TaskDescriptor &descriptor);
QString categoryLabel(const QString &category);
QString parameterLabel(const ParameterSpec &parameter);
QString choiceLabel(const QString &parameter, const QString &value);
QString portLabel(const QString &port);
QString portLabel(const TaskDescriptor &descriptor, const QString &port);
QString resultError(const NodeResult &result);
QString stateLabel(NodeState state);
QColor stateColor(NodeState state);
QIcon methodIcon(bool source);
QIcon runIcon();
void applyTheme(QApplication &app);
} // namespace aster::ui
#endif
