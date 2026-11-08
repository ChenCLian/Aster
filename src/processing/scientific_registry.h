// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PROCESSING_SCIENTIFIC_REGISTRY_H
#define ASTER_PROCESSING_SCIENTIFIC_REGISTRY_H
#include "core/task_registry.h"
#include "processing/example_source_task.h"
#define SCIENTIFIC_METHODS(X)                                                                      \
    X(synthetic_orbital, SyntheticOrbitalTask) \
    ODP_EXAMPLE_METHODS(X) \
    X(database_series, DatabaseSeriesTask)                                                         \
    X(database_spectrum, DatabaseSpectrumTask)                                                     \
    X(database_grid, DatabaseGridTask)                                                             \
    X(database_table, DatabaseTableTask)                                                           \
    X(file_source, FileSourceTask)                                                                 \
    X(preprocess, PreprocessTask)                                                                  \
    X(detrend_lowess, LowessTask)                                                                  \
    X(mtm, MtmTask)                                                                                \
    X(mtm_ar1_simulation, MtmSimulationTask)                                                        \
    X(ar1_global, Ar1GlobalTask)                                                                    \
    X(cwt, WaveletTask) \
    X(orbital_source, OrbitalSourceTask) \
    X(etp_source, EtpSourceTask) \
    X(insolation_source, InsolationSourceTask) \
    X(seasonal_insolation, SeasonalInsolationTask) \
    X(clean_series, CleanSeriesTask) \
    X(gaussian_filter, GaussianFilterTask) \
    X(taner_filter, TanerFilterTask) \
    X(hilbert_envelope, HilbertEnvelopeTask) \
    X(insolation_map, InsolationMapTask) \
    X(eccentricity_source, EccentricitySourceTask) \
    X(precession_tilt_source, PrecessionTiltSourceTask) \
    X(evolution_fft, EvolutionFftTask) \
    X(evolution_mtm, EvolutionMtmTask) \
    X(select_interval, SelectIntervalTask) \
    X(demean, DemeanTask) \
    X(standardize, StandardizeTask) \
    X(sampling_check, SamplingCheckTask) \
    X(moving_average, MovingAverageTask) \
    X(savitzky_golay, SavitzkyGolayTask) \
    X(fft_spectrum, FftSpectrumTask) \
    X(cross_spectrum, CrossSpectrumTask) \
    X(welch_spectrum, WelchSpectrumTask) \
    X(lomb_scargle, LombScargleTask) \
    X(butterworth_lowpass, ButterworthLowpassTask) \
    X(butterworth_highpass, ButterworthHighpassTask) \
    X(sedrate_to_age, SedrateToAgeTask) \
    X(apply_age_model, ApplyAgeModelTask) \
    X(milankovitch_period, MilankovitchPeriodTask) \
    X(cycle_age_model, CycleAgeModelTask) \
    X(coco_scan, CocoTask) \
    X(coco_monte_carlo, CocoMonteCarloTask) \
    X(evolution_coco, CocoEvolutionTask)
namespace aster
{
namespace MethodKeys
{
#define DECLARE_METHOD(method_id, type) inline constexpr const char *method_id = #method_id;
SCIENTIFIC_METHODS(DECLARE_METHOD)
#undef DECLARE_METHOD
} // namespace MethodKeys
TaskRegistry makeScientificRegistry();
} // namespace aster
#endif
