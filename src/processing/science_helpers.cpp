#include "processing/science_helpers.h"
#include <gsl/gsl_errno.h>
#include "math/backend/numeric_backend.h"
#include <mutex>
namespace aster::science_detail {
QJsonObject numericBackendReport()
{
    const auto info = numeric::backend::backendInfo();
    return {{"provider", QString::fromStdString(info.name)},
            {"linear_algebra", QString::fromStdString(info.linearAlgebra)},
            {"fft", QString::fromStdString(info.fft)},
            {"threads_per_call", info.threadsPerCall}};
}
void initializeGsl()
{
    static std::once_flag once;
    std::call_once(once, [] { gsl_set_error_handler_off(); });
}
}
