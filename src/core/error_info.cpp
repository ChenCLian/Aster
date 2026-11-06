// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "core/error_info.h"
namespace aster
{
namespace
{
QJsonObject smallContext(const QJsonObject &input)
{
    QJsonObject result;
    for (auto it = input.begin(); it != input.end() && result.size() < 64; ++it)
    {
        if (it.key().size() > 80 || it->isArray() || it->isObject()) continue;
        result.insert(it.key(), it->isString() ? QJsonValue(it->toString().left(1024)) : *it);
    }
    return result;
}
}
QString errorCodeName(ErrorCode code)
{
    switch (code)
    {
#define ASTER_ERROR_NAME(name, key, fallback) case ErrorCode::name: return QStringLiteral(key);
        ASTER_ERROR_CODES(ASTER_ERROR_NAME)
#undef ASTER_ERROR_NAME
    }
    return QStringLiteral("UNCLASSIFIED");
}
QStringList builtinErrorCodes()
{
    return {
#define ASTER_ERROR_KEY(name, key, fallback) QStringLiteral(key),
        ASTER_ERROR_CODES(ASTER_ERROR_KEY)
#undef ASTER_ERROR_KEY
    };
}
QString errorFallback(const ErrorInfo &error)
{
    if (!error.technical.isEmpty()) return error.technical;
#define ASTER_ERROR_FALLBACK(name, key, fallback) if (error.code == QLatin1String(key)) return QStringLiteral(fallback);
    ASTER_ERROR_CODES(ASTER_ERROR_FALLBACK)
#undef ASTER_ERROR_FALLBACK
    return error.code.isEmpty() ? QString() : error.code;
}
ErrorInfo makeError(ErrorCode code, QJsonObject context, QString technical)
{
    return {errorCodeName(code), smallContext(context), technical.left(4096)};
}
QJsonObject ErrorInfo::toJson() const
{
    if (isEmpty()) return {};
    return {{"version", 1}, {"code", code.left(128)}, {"context", smallContext(context)},
            {"technical", technical.left(4096)}};
}
ErrorInfo ErrorInfo::fromJson(const QJsonObject &object)
{
    if (object.value("version").toInt() != 1 || !object.value("code").isString()) return {};
    return {object.value("code").toString().left(128),
            smallContext(object.value("context").toObject()),
            object.value("technical").toString().left(4096)};
}
} // namespace aster
