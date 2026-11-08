// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/synthetic_orbital_task.h"
#include "processing/cross_spectrum_task.h"
#include "processing/coco_analysis_tasks.h"
#include "processing/coco_task.h"
#include "processing/cycle_tuning_tasks.h"
#include "processing/chronology_tasks.h"
#include "processing/spectral_extension_tasks.h"
#include "processing/basic_signal_tasks.h"
#include "processing/evolutionary_tasks.h"
#include "processing/series_utility_tasks.h"
#include "processing/scientific_registry.h"
#include "processing/astronomy_source_tasks.h"
#include "processing/filter_tasks.h"
#include "processing/insolation_map_task.h"
#include "processing/orbital_table_tasks.h"
#include "processing/builtin_tasks.h"
#include "processing/file_source_task.h"
#include "processing/database_source_task.h"
#include "processing/preprocess_task.h"
#include "processing/clean_series_task.h"
#include "processing/lowess_task.h"
#include "processing/mtm_task.h"
#include "processing/mtm_simulation_task.h"
#include "processing/ar1_global_task.h"
#include "processing/wavelet_task.h"
#include "processing/science_helpers.h"
#include <stdexcept>
namespace aster
{
TaskRegistry makeScientificRegistry()
{
    science_detail::initializeGsl();
    auto registry = makeBuiltinRegistry();
#define REGISTER_METHOD(method_id, type)                                                           \
    {                                                                                              \
        auto descriptor = type::descriptor();                                                      \
        descriptor.key = MethodKeys::method_id;                                                    \
        const auto result = registry.add(descriptor, [](const QJsonObject &p)                      \
                                         { return std::make_unique<type>(p); });                   \
        if (!result.ok())                                                                          \
            throw std::logic_error(result.message().toStdString());                                \
    }
    SCIENTIFIC_METHODS(REGISTER_METHOD)
#undef REGISTER_METHOD
    return registry;
}
} // namespace aster
