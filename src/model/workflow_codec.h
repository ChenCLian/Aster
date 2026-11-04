#ifndef ASTER_MODEL_WORKFLOW_CODEC_H
#define ASTER_MODEL_WORKFLOW_CODEC_H

#include "model/workflow_types.h"

namespace aster {
struct WorkflowReadResult {
    WorkflowDefinition definition;
    ValidationResult validation;
};
QJsonObject workflowToJson(const WorkflowDefinition& definition);
// Structural decoding only; WorkflowModel::replaceDefinition also validates
// task versions, parameter schemas, ports, cycles and grid relationships.
WorkflowReadResult workflowFromJson(const QJsonObject& json);
} // namespace aster
#endif
