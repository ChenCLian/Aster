#ifndef ASTER_PROCESSING_MTM_TASK_H
#define ASTER_PROCESSING_MTM_TASK_H
#include "core/task_registry.h"
namespace aster {
class MtmTask final : public Task {
public:
    explicit MtmTask(const QJsonObject& parameters);
    static TaskDescriptor descriptor();
    NodeResult execute(const TaskInput&, const ExecutionContext&) override;
private:
    int m_peakDistance, m_nfft, m_peakRadius;
    double m_nw, m_fmaxRatio, m_smoothWindow, m_prominence, m_peakWidth, m_peakHeight, m_relativeProminence;
    bool m_ar1, m_swa, m_logfit;
};
}
#endif
