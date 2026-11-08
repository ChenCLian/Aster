// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_JOINT_BATCH_RUNNER_H
#define ASTER_JOINT_BATCH_RUNNER_H
#include "application/batch_runner.h"
#include "core/batch_workflow.h"
namespace aster
{
BatchResult runJointBatch(std::shared_ptr<const BatchSnapshot>, const QString &databasePath,
                          int concurrency, EventSink events = {}, CompletionSink completed = {},
                          CancellationToken cancel = {},
                          quint64 retainedResultBudget = 512ull * 1024 * 1024);
}
#endif
