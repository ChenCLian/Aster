// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "processing/builtin_tasks.h"
#include <stdexcept>

namespace aster {
TaskRegistry makeBuiltinRegistry()
{
    TaskRegistry registry;
#define REGISTER_TASK(method_id, type) \
    { \
        auto descriptor = type::descriptor(); \
        descriptor.key = QString::fromLatin1(MethodKeys::method_id); \
        const auto result = registry.add(std::move(descriptor), [](const QJsonObject& p) { return std::make_unique<type>(p); }); \
        if (!result.ok()) throw std::logic_error(result.message().toStdString()); \
    }
    PROCESS_METHODS(REGISTER_TASK)
#undef REGISTER_TASK
    return registry;
}
} // namespace aster
