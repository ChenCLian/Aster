#include "core/task.h"

namespace aster {
QString nodeStateName(NodeState state)
{
    switch (state) {
#define STATE_NAME(name) case NodeState::name: return QStringLiteral(#name);
        ASTER_NODE_STATES(STATE_NAME)
#undef STATE_NAME
    }
    return QStringLiteral("Unknown");
}
NodeResult NodeResult::success(QMap<QString, DataRef> data, QJsonObject info)
{
    NodeResult result;
    result.state = NodeState::Succeeded;
    result.outputs = std::move(data);
    result.report = std::move(info);
    return result;
}
NodeResult NodeResult::failure(QString error)
{
    NodeResult result;
    result.error = std::move(error);
    result.setDiagnostic(makeError(ErrorCode::Unknown, {}, result.error));
    return result;
}
NodeResult NodeResult::failure(ErrorInfo error)
{
    NodeResult result;
    result.error = errorFallback(error);
    result.setDiagnostic(error);
    return result;
}
ErrorInfo NodeResult::diagnostic() const
{
    return ErrorInfo::fromJson(report.value("diagnostic").toObject());
}
void NodeResult::setDiagnostic(const ErrorInfo &error)
{
    if (!error.isEmpty()) report.insert("diagnostic", error.toJson());
}
NodeResult NodeResult::cancelled()
{
    NodeResult result;
    result.state = NodeState::Cancelled;
    result.error = QStringLiteral("Cancellation requested");
    result.setDiagnostic(makeError(ErrorCode::Cancelled));
    return result;
}
} // namespace aster
