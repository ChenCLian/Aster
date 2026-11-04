#ifndef ASTER_ERROR_PRESENTER_H
#define ASTER_ERROR_PRESENTER_H
#include "core/task.h"
namespace aster::ui
{
struct ErrorPresentation
{
    QString summary;
    QString suggestion;
    QString details; // Plain text; safe for display and copying.
};
ErrorPresentation presentError(const ErrorInfo &error, const QString &fallback = {});
ErrorPresentation presentError(const NodeResult &result);
} // namespace aster::ui
#endif
