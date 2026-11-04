#ifndef ASTER_PROCESSING_PROCESS_DEFINITIONS_H
#define ASTER_PROCESSING_PROCESS_DEFINITIONS_H

// Stable key + implementation class. One catalogue entry per new task.
// Keys are persisted; never change them to match a translated display label.
#define PROCESS_METHODS(X)                    \
    X(input_series, InputSeriesTask)          \
    X(sine_source, SineSourceTask)            \
    X(linear_detrend, LinearDetrendTask)      \
    X(affine, AffineTask)                     \
    X(series_summary, SeriesSummaryTask)

#endif
