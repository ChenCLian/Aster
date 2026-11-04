#ifndef ASTER_MODEL_WORKFLOW_TYPES_H
#define ASTER_MODEL_WORKFLOW_TYPES_H

#include <QJsonObject>
#include <QMap>
#include <QUuid>
#include <QVector>
#include <optional>

namespace aster {
using NodeId = QUuid;
using RunId = QUuid;
enum class ChildSlot { Left, Right, Down };

struct GridPosition {
    int column = 0, row = 0;
    bool operator==(const GridPosition& other) const
    { return column == other.column && row == other.row; }
};
constexpr int GridCoordinateLimit = 1000000;
constexpr qsizetype WorkflowNodeLimit = 10000;
bool validPosition(GridPosition position);
std::optional<GridPosition> adjacentPosition(GridPosition parent, ChildSlot slot);
QString slotName(ChildSlot slot);
std::optional<ChildSlot> slotFromName(const QString& name);

// The sole authoritative edge is stored on the child. There is no second
// editable children array. Parent/child lookup indexes are derived from this.
struct InputBinding {
    NodeId parent;
    QString output;
    ChildSlot slot = ChildSlot::Down; // Editor placement, never scheduling priority.
};
struct NodeDefinition {
    NodeId id;
    QString method;                 // Stable key, never display name or list index.
    int methodVersion = 1;
    QJsonObject parameters;
    std::optional<InputBinding> input;
};
struct WorkflowDefinition {
    QVector<NodeDefinition> nodes;  // Creation order; stable sibling execution order.
    QMap<NodeId, GridPosition> layout;
};
struct ValidationIssue { QString path, message; };
struct ValidationResult {
    QVector<ValidationIssue> issues;
    bool ok() const { return issues.isEmpty(); }
    void add(const QString& path, const QString& message) { issues.push_back({path, message}); }
    QString message() const;
};

} // namespace aster
#endif
