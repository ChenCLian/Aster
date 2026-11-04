#ifndef ASTER_PROCESSING_TASK_CATEGORIES_H
#define ASTER_PROCESSING_TASK_CATEGORIES_H
#include <QStringList>

// Stable category tokens; both task-menu surfaces translate category.<token>.
// Keep the existing Preprocess root token for existing UI ordering, but present
// it as Data processing. No task IDs, method versions or output keys change.
#define ASTER_TASK_CATEGORIES(X)                                                                   \
    X(OdpExamples, "Data sources", "ODP examples") \
    X(Files, "Data sources", "Files")                                                              \
    X(StoredData, "Data sources", "Database")                                                      \
    X(ExistingData, "Data sources", "Existing data")                                               \
    X(TestSignals, "Data sources", "Test signals")                                                 \
    X(Orbits, "Data sources", "Astronomy", "Orbital solutions")                                    \
    X(Insolation, "Data sources", "Astronomy", "Insolation")                                       \
    X(Targets, "Data sources", "Astronomy", "Target periods") \
    X(Selection, "Preprocess", "Selection")                                                        \
    X(Cleanup, "Preprocess", "Cleanup")                                                            \
    X(Sampling, "Preprocess", "Sampling")                                                          \
    X(Chronology, "Preprocess", "Age scale") \
    X(Amplitude, "Preprocess", "Amplitude")                                                        \
    X(Trend, "Preprocess", "Trend")                                                                \
    X(Smoothing, "Preprocess", "Smoothing") \
    X(Filtering, "Preprocess", "Filtering")                                                        \
    X(PowerSpectrum, "Analysis", "Spectrum", "Power spectrum")                                     \
    X(Significance, "Analysis", "Spectrum", "Significance")                                        \
    X(Wavelet, "Analysis", "Time-frequency", "Wavelet")                                            \
    X(Evolution, "Analysis", "Time-frequency", "Evolutionary spectrum")                            \
    X(Envelope, "Analysis", "Amplitude")                                                           \
    X(OrbitalMatching, "Analysis", "Astrochronology", "Orbital matching") \
    X(Summary, "Analysis", "Statistics")                                                           \
    X(Quality, "Analysis", "Data quality")

namespace aster
{
enum class TaskCategory
{
#define ASTER_CATEGORY_ENUM(name, ...) name,
    ASTER_TASK_CATEGORIES(ASTER_CATEGORY_ENUM)
#undef ASTER_CATEGORY_ENUM
};
inline QStringList taskCategory(TaskCategory category)
{
    switch (category)
    {
#define ASTER_CATEGORY_PATH(name, ...)                                                             \
    case TaskCategory::name:                                                                       \
        return {__VA_ARGS__};
        ASTER_TASK_CATEGORIES(ASTER_CATEGORY_PATH)
#undef ASTER_CATEGORY_PATH
    }
    return {};
}
} // namespace aster
#endif
