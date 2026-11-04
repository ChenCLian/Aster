#ifndef ASTER_APPLICATION_EXECUTION_DEFAULTS_H
#define ASTER_APPLICATION_EXECUTION_DEFAULTS_H
#include <QThread>
#include <algorithm>

namespace aster
{
inline constexpr int minimumConcurrency = 1;
inline constexpr int maximumConcurrency = 32;

inline int defaultConcurrency()
{
    // Independent input runs can each hold large spectral/wavelet workspaces.
    // Eight is a conservative default cap, not a per-algorithm performance claim.
    return std::clamp(QThread::idealThreadCount(), minimumConcurrency, 8);
}
} // namespace aster
#endif
