#ifndef ASTER_CORE_TASK_H
#define ASTER_CORE_TASK_H

#include "model/data_types.h"
#include "model/workflow_types.h"
#include "core/error_info.h"
#include <atomic>
#include <functional>

namespace aster
{
#define ASTER_NODE_STATES(X) X(Pending) X(Running) X(Succeeded) X(Failed) X(Blocked) X(Cancelled)
enum class NodeState
{
#define DECLARE_STATE(name) name,
    ASTER_NODE_STATES(DECLARE_STATE)
#undef DECLARE_STATE
};
QString nodeStateName(NodeState state);

class CancellationToken
{
  public:
    CancellationToken() : m_flag(std::make_shared<std::atomic_bool>(false)) {}
    void cancel() const
    {
        m_flag->store(true, std::memory_order_relaxed);
    }
    bool isCancelled() const
    {
        return m_flag->load(std::memory_order_relaxed);
    }

  private:
    std::shared_ptr<std::atomic_bool> m_flag;
};

struct TaskInput
{
    DataRef data;
}; // Empty for source tasks.
struct ExecutionContext
{
    RunId runId;
    NodeId nodeId;
    CancellationToken cancellation;
    DataRef boundSource;        // Optional source binding supplied by the application.
    QJsonObject sourceMetadata; // Frozen per-input locator/options; e.g. a file path.
    // Optional synchronous worker callback. Percent is local to this node;
    // message is a stable presentation key. Never transports numerical arrays.
    std::function<void(int, const QString&)> progress;
};
struct NodeResult
{
    NodeState state = NodeState::Failed;
    QMap<QString, DataRef> outputs;
    QJsonObject report;
    QString error;
    QStringList warnings;
    static NodeResult success(QMap<QString, DataRef> outputs, QJsonObject report = {});
    static NodeResult failure(QString error);
    static NodeResult failure(ErrorInfo error);
    ErrorInfo diagnostic() const;
    void setDiagnostic(const ErrorInfo &error);
    static NodeResult cancelled();
};

class Task
{
  public:
    Task() = default;
    virtual ~Task() = default;
    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;
    Task(Task &&) = delete;
    Task &operator=(Task &&) = delete;
    virtual NodeResult execute(const TaskInput &input, const ExecutionContext &context) = 0;
};

struct RunEvent
{
    RunId runId;
    NodeId nodeId;
    NodeState state = NodeState::Pending;
    QString message;
    NodeId root;
    QString sourceLabel;
    ErrorInfo diagnostic;
    int progressPercent = -1; // -1 = state transition, 0..100 = Running progress.
};
// Called synchronously on the executor's thread. UI adapters must queue it to
// the GUI thread. Events contain identifiers/status, not large numerical data.
using EventSink = std::function<void(const RunEvent &)>;
} // namespace aster
#endif
