#ifndef ASTER_PROCESSING_BUILTIN_TASKS_H
#define ASTER_PROCESSING_BUILTIN_TASKS_H

#include "core/task_registry.h"
#include "processing/process_definitions.h"
#include "processing/input_series_task.h"
#include "processing/sine_source_task.h"
#include "processing/linear_detrend_task.h"
#include "processing/affine_task.h"
#include "processing/series_summary_task.h"

namespace aster {
namespace MethodKeys {
#define DECLARE_KEY(key, type) inline constexpr const char* key = #key;
    PROCESS_METHODS(DECLARE_KEY)
#undef DECLARE_KEY
}

// All factories receive already-normalized JSON. Algorithms store typed,
// per-instance parameters. No global static setters are used.
TaskRegistry makeBuiltinRegistry();
} // namespace aster
#endif
