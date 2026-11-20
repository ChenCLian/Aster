# Spectral estimation, cross spectra, wavelets and AR1 tests

[Statistical interpretation](../concepts/statistical_interpretation_en.md) · [Windows and units](../concepts/spectral_parameters_en.md) · [中文](spectral_inference.md)

This guide explains operation and interpretation. Detailed formulas and historical evidence remain in the linked method documents. See the [power-spectrum guide](power_spectra_en.md) for FFT, Welch and evolutionary spectra.

<a id="mtm"></a>
## Multitaper spectrum

Input must be finite, strictly increasing and regularly spaced, with at least four points and NW<N/2. Default NW=2 therefore requires at least five points. The task removes the mean, without automatic linear detrending or resampling. Constants give zero power and no background significance evaluation.

Choose NW before FFT length. NW is the time–half-bandwidth product, either 1 or 1.25–32. NW=1 uses two DPSS tapers; otherwise the count is `round(2NW)−1`. Larger NW usually smooths the spectrum and reduces separation of nearby peaks. Half-bandwidth is `NW/(N×Δx)`, distinct from FFT bin spacing. FFT 0 uses N points; an explicit value must be at least N. Padding adds no observation duration.

`spectrum` is one-sided physical PSD, with Y²×X units. The maximum frequency crops published outputs only. Background fitting, adaptive convergence, FDR and peak detection use the full spectrum before in-range peaks are retained. Older records may retain a legacy ratio limit; inspect the reported output range.

AR1 and SWA are enabled by default. The AR1 median-smoothing window is a fraction of frequency-bin count, not input sample count. Logarithmic fitting changes the background fit, not Y or the display scale. SWA uses its own smoothing rules rather than this AR1 window fraction. See [FDR and nominal thresholds](../concepts/statistical_interpretation_en.md#fdr).

A failed background fit may omit only the associated optional ports while retaining spectrum and peaks. Reaching the adaptive iteration limit also retains results with a warning. Successful task completion does not imply every optional estimate is available.

Limits include one million input samples, four million FFT points and additional taper/workspace budgets. Parameter maxima cannot necessarily be combined. Cropping frequencies does not reduce the main full-spectrum workspace. [Parameters](../reference/task_parameters_en.md#m-mtm).

<a id="peaks"></a>
## MTM peak table

Detection finds interior local maxima, excluding endpoints; flat tops use a middle index. Effective prominence is the maximum of absolute prominence, the requested fraction of the full spectrum's range and a roundoff floor. Default relative prominence is 0.1%, not a significance level.

Width is measured at half prominence in bins and may be fractional. Distance also uses bins. Radius zero searches the full range; a positive radius limits baseline search and can alter both prominence and width. Competing candidates are prioritized by prominence, then height, then position.

The table includes frequency, period, power, prominence, width_bins and AR1/SWA 95% and FDR 0.05 flags. Values 1/0/−1 mean passed/not passed/not evaluated or unavailable, not probabilities. Period is inverse frequency in X units. Database frequency indexing is a retrieval feature, not confirmation of astronomical cycles.

<a id="lomb"></a>
## Lomb–Scargle

At least five finite samples, strictly increasing X and nonconstant Y are required. Irregular sampling is allowed; no sorting, gap filling or resampling occurs. Equal-weight sine/cosine fits include a floating intercept. Per-observation uncertainty weights are not provided.

Minimum frequency 0 selects `1/T`, where T is the endpoint X span. Maximum 0 selects `0.5/median adjacent spacing`, a search heuristic rather than a strict Nyquist limit for irregular data. Choose an explicit band when justified, with maximum greater than minimum.

Default samples-per-peak is 5, giving frequency step `1/(T×samples_per_peak)`. Increasing it samples the grid more densely without adding independent information. The last output bin may fall below the requested maximum; inspect first_frequency, last_frequency, frequency_step and grid_points.

`periodogram` is `(chi0−chi_f)/chi0`, the residual-sum-of-squares improvement over a constant fit, dimensionless in 0–1. It is not PSD and has no FAP or significance threshold. Do not compare its peak height directly with MTM PSD. Rank-deficient fitting bases are reported through rank_one_bins and rank_zero_bins; inspect sampling and frequency choices.

Limits are one million samples, 65536 frequency bins and `samples×bins≤50 million`. Narrow the band or reduce density when justified. Constants cannot be normalized. [Parameters](../reference/task_parameters_en.md#m-lomb_scargle).

<a id="cross"></a>
## Welch cross spectrum

Configure A/B pairs in the same workspace. Inputs must be finite, equal-length, strictly increasing and regularly spaced on a shared X grid. Trimmed X names and units must match; renaming labels is not a physical unit or age-reference conversion. Y units may differ.

Defaults are segment 256, overlap 128, periodic Hann and segment mean removal. At least two complete segments are required, hence at least 384 samples with defaults. Changing segment length does not adjust overlap. FFT 0 uses segment length; explicit FFT length cannot be shorter. Incomplete tails are reported and omitted. Both inputs use the same processing settings without automatic common-interval cropping or padding.

Complex spectra `conj(A)×B` are averaged before magnitude, squared coherence and phase are calculated. This is not averaging segment magnitudes. Coherence is dimensionless in 0–1, not a p-value; no significance threshold is provided.

Phase follows increasing numeric X. Swapping A/B reverses its sign modulo a full turn. Ages increasing into the past must not be interpreted as forward-time leads/lags without conversion. Phase may be unstable at low power or coherence. No automatic phase unwrapping or time-delay conversion is performed.

Zero auto-power makes coherence undefined; zero complex cross-power makes phase undefined. Rectangular taper with mean/linear detrending explicitly zeros DC and masks these metrics; Hann does not use this special rule. Undefined values appear as “—”, empty CSV cells and curve breaks, not valid zeros.

See [complete columns, units and budgets](cross_spectrum.md) and [pairing instructions](../USER_GUIDE_EN.md#cross-spectrum).

<a id="wavelet"></a>
## Continuous wavelet transform

The transform checks cancellation between scales and while preparing power and significance maps. Cancelling discards the partial output of this node.

Input must be finite, strictly increasing and regularly spaced, with at least six samples. The task removes the mean but not an arbitrary long-term trend. Defaults are Morlet parameter 6 and dj=0.1. Morlet requires parameter≥5; Paul requires an integer order and DOG an even integer, within the UI ranges. Recheck meaning and boundary conventions after changing the mother wavelet.

dj controls log2 scale spacing. Smaller dj makes a denser, more expensive grid without improving the original record's resolving capability. Period is converted from scale and is not scale itself. Columns are record positions and rows are periods, not frequencies.

Default variance-normalized, scale-corrected power is `|W|²/(sample variance×physical scale)`, with inverse-X units; raw_power and scale_corrected_power are also available. Raw-power mode instead uses `|W|²` in Y² units and changes scale coverage and AR1 estimation conventions. It is not only a color change. Constants fail in default mode but can produce zero raw power.

When enabled and available, significance_ratio divides raw power by its corresponding nominal local 95% threshold. It is not an arbitrary curve applied to the displayed normalized power. Invalid backgrounds may suppress this output while retaining power. See [local thresholds](../concepts/statistical_interpretation_en.md#local-global) and [COI](../concepts/statistical_interpretation_en.md#coi).

Besides the one-million-sample limit, the grid is limited to 16 million cells and a 512 MiB estimated workspace. Default mode retains several grids; memory cannot be inferred from one plotted array alone. Review input length and dj when limits are exceeded. See [detailed conventions and historical names](cwt.md).

<a id="ar1"></a>
## AR1 background global test

Connect a series directly; an MTM node is not a prerequisite. Input needs 64–1024 finite, strictly increasing, regularly spaced, nonconstant samples. The hypothesis is stationary Gaussian AR1 noise; input validation does not establish it.

NW=2, K=3, NFFT=N and the tested bin set are fixed. No post-peak narrowing of the test band is offered. Standard precision uses 999 screening simulations **per reference model**, with the first 199 also used for analysis statistics. Fine uses 999 analysis simulations. These are not total simulations for the whole task. Fine mode costs more and can exceed budgets for some larger N settings.

Internal LOWESS is disabled by default. If enabled, fraction defaults to 0.35, with ordinary LOWESS and zero robust iterations, applied to observations and references alike. The fraction only matters when enabled. Upstream processing is not replayed. Use the seed for reproduction rather than selecting a significant outcome.

Caching is enabled by default and reuses matching references only; the observation statistic is always recomputed. Disabling it does not change the test. Regenerating the same settings and seed is not new independent evidence. References contain no original user series; invalid caches are regenerated and write failures can warn without discarding the calculated result.

Default `ar1_global_test`: read test_available before global_p and reject_global_null. Alpha is fixed at 0.05; rejection requires available=1 and p≤alpha. Empty candidate sets give available=0 and p=1, meaning inconclusive. Additional outputs are spectrum, ar1_global_background and ar1_analysed_series.

Inference concerns one input over its predefined frequency set, not per-peak probabilities, whole-map CWT inference or astronomical attribution. See [mathematical basis, cache and historical calibration](ar1_global.md).

<a id="legacy"></a>
## Archived experimental MTM AR1 test

`mtm_ar1_simulation` is hidden from the task catalogue but retains its identity for historical records. Its estimated-rho approximate bootstrap is not the current AR1 global test. Independently specified rho mode cannot treat an estimate from this record as prior known information. Similar parameter names do not make the interpretations interchangeable. Read historical results with their original method version and settings. [Archived scope](statistical_limits.md#已隐藏的旧实验任务).
