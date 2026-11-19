# FFT, Welch and evolutionary power spectra

[Shared concepts](../concepts/spectral_parameters_en.md) · [Methods](README.md) · [中文](power_spectra.md)

This guide covers full-record FFT, Welch, evolutionary FFT and evolutionary MTM. All require finite paired X/Y data with strictly increasing, uniform X. They do not silently sort or resample; use [sampling inspection](preprocessing_en.md#inspection) when necessary.

| Question | Task | Output |
| --- | --- | --- |
| Periodic structure across the full record | FFT power spectrum | Full-record one-sided PSD |
| Average estimates from several segments | Welch power spectrum | Arithmetic mean of complete-segment PSDs |
| How structure changes with time/depth | Evolutionary FFT | Moving-window map and mean window spectrum |
| Position-dependent multitaper estimates | Evolutionary MTM | Adaptive MTM map and mean window spectrum |

These outputs are descriptive: none supplies p values or spectral/map significance thresholds. Evolutionary MTM does not inherit every inference output of the separate MTM task.

<a id="fft"></a>
## FFT power spectrum

The entire input forms one segment, with at least 8 samples. Defaults are demeaning, periodic Hann and FFT length 0, meaning input length. An explicit FFT length must accommodate the entire input. Output begins at zero frequency and ends at the highest available one-sided grid point.

Choose the input interval and detrending, then the taper. Increase FFT length when denser sampling around a peak is useful. See [zero padding](../concepts/spectral_parameters_en.md#fft-padding) for its limits. The `spectrum` values are PSD, not complex FFT coefficients or signal amplitude.

Defaults and ranges: [FFT reference](../reference/task_parameters_en.md#m-fft_spectrum).

<a id="welch"></a>
## Welch power spectrum

Defaults are 256 samples per segment, 128 overlapping samples, per-segment demeaning and periodic Hann. Segment length cannot exceed input length; overlap must be smaller than segment length. For input shorter than 256 samples, explicitly choose a suitable shorter segment and recheck overlap.

Choose length, then overlap, then FFT length. FFT 0 uses segment length; explicit values cannot be shorter. Longer segments generally separate neighboring frequencies better but usually leave fewer estimates to average. Shorter segments provide more averaging opportunities with less frequency resolution per estimate. Overlapping segments are not independent observations.

Only complete segments are averaged; the tail is not padded. For 705 samples, length 128 and overlap 64, the step is 64 and there are 10 segments. The last used zero-based sample index is 703, leaving one unused sample. This is a conversion example, not a recommendation.

`spectrum` is the arithmetic mean PSD; no segment-by-frequency matrix is stored. Use an evolutionary task to retain variation with position. The report records actual segments, overlap, FFT length and unused tail.

Reference: [Welch parameters](../reference/task_parameters_en.md#m-welch_spectrum).

<a id="evolution-settings"></a>
## Setting up evolutionary FFT and MTM

### 1. Choose an analysis window

New evolutionary FFT tasks use 20% input span, a target of 500 windows and 4× FFT. Evolutionary MTM retains 128-sample windows, 16-sample steps and FFT length 0. Windows require at least 8 samples and cannot exceed current input length. Current input is the upstream output, not necessarily the imported file's row count.

For span percentage p, the implementation uses `L=round((N−1)×p/100)`, rounding positive half-integers upward. Fewer than 8 samples causes an error, without automatic enlargement. Actual endpoint span is still `(L−1)×dx`. Thus 100% produces N−1 samples, not a full-record FFT. To explicitly use all N observations, select sample-count mode and enter N.

### 2. Choose position density

- Samples: specify integer step S with `1≤S≤L`.
- Overlap: `S=max(1,round(L×(1−overlap/100)))`. Read actual overlap from the report.
- Target windows: for K≥2, `S=max(1,ceil((N−L)/(K−1)))`. Complete-window count does not exceed K, and integer rounding may leave substantially fewer windows. Windows are not repeated to fill the target.

For N=1001, L=200 and K=500, step is 2, count is 401, overlap is 99% and one tail sample is unused. If too small a target requires S>L, the task reports the minimum feasible target instead of silently leaving gaps between windows.

### 3. Select estimator settings

Evolutionary FFT offers periodic Hann or rectangular tapers. Evolutionary MTM uses adaptive DPSS multitaper estimation. NW must be 1 or within 1.25–32, and below L/2. NW=1 uses two tapers; otherwise the count is `round(2×NW)−1`. NW is not the number of moving positions. Larger NW generally smooths the estimate more and may reduce separation of nearby peaks.

Both tasks offer per-window demeaning or linear detrending. FFT length may be explicit or an integer multiple of window length. A multiplier of 1 means no padding. See [detrending and tapers](../concepts/spectral_parameters_en.md#taper-trend) and [padding](../concepts/spectral_parameters_en.md#fft-padding).

### 4. Confirm frequency range and normalization

The upper limit is in reciprocal X units. Zero selects the Nyquist range; larger values are clamped. A band containing fewer than two output frequency points is rejected. A saved legacy frequency ratio remains active until explicitly editing the limit, including confirming 0. Actual maximum output frequency may be below the request because output lies on a finite grid.

Per-window normalization defaults on for new evolutionary FFT tasks and off for evolutionary MTM. Retain PSD to compare absolute power, with consistent units and processing. Normalize when comparing spectral shape, while relinquishing absolute-strength interpretation. See [normalization](../concepts/spectral_parameters_en.md#normalization).

<a id="evolution-output"></a>
## Outputs and resolved settings

`power_map` uses the midpoint of each window's endpoint X coordinates horizontally, and frequency vertically. Internally, rows are frequencies and columns are windows. Only complete windows are used, so the map normally does not extend to the original record's endpoints. A window center is not an instantaneous measurement.

`mean_spectrum` averages the output powers of all windows: relative power when normalization is enabled, otherwise PSD. It is not a full-record estimate. Square-root display does not turn this output into mean amplitude.

After a successful preview, the area between the lower node-output selector and its plot shows resolved length, step, overlap and FFT length; hover for count, spans and tail information. Wait for a new preview after changes. These values also appear in the calculation report; use actual values when reproducing an analysis.

<a id="preset"></a>
## Using the evolutionary FFT preset

New evolutionary FFT tasks use this default configuration directly: 20% span, target 500 windows, 4× FFT, linear detrending, rectangular taper, per-window normalization, smooth square-root power display and maximum frequency 0 (automatic). No preset button is needed; every setting remains editable. Evolutionary MTM keeps its own defaults.

The saved settings are rules—percentages, target counts and multipliers. Each preview and run resolves them from the actual input to this node, including datasets whose editor has never been opened. Batch application does not copy another dataset’s resolved sample counts. Windows still require at least 8 samples; short inputs fail explicitly rather than silently switching settings.

**Restore default parameters** affects this node for the current dataset or pair, including its automatic frequency limit. Use the batch button afterwards to restore defaults for this node across the workflow. Explicit settings in saved workflows remain unchanged; older fixed sample counts without mode fields retain their fixed-count interpretation. See [parameter scope and automatic settings](../USER_GUIDE_EN.md#parameter-scope).

First check whether the 20% window contains the periods of interest, then inspect actual counts and cost. The target 500 is an upper count, not a guaranteed output count. The preset changes both calculation and display, so its effects cannot all be described as smoothing.

Some choices reference Acycle LAH, without promising identical defaults or boundaries across versions. Aster does not pad both ends of the record, resample automatically or add a two-dimensional smoothing filter. Align numerical settings and coordinates before [display settings](../concepts/spectral_parameters_en.md#display). Sources and earlier comparison scope remain in the [method comparison (Chinese)](acycle_compatibility.md).

<a id="limits"></a>
## Errors and calculation size

| Symptom | First checks |
| --- | --- |
| Unordered/nonuniform input | X units, sampling inspection and explicit preprocessing; more FFT points will not fix sampling |
| Oversized window or fewer than 8 samples | Actual upstream input and percentage conversion; choose the research scale first |
| Step exceeds window / target too small | Increase target or window length, following the diagnostic |
| FFT shorter than segment/window | Increase FFT length or use 0, whose meaning is task-specific |
| Fewer than two output frequency points | Frequency units and limit; do not enter a period directly in a frequency field |
| Grid/work budget exceeded | Reduce positions, FFT length or input size as appropriate; frequency cropping reduces output size, not FFT length |

Current limits are one million input samples, 262144 FFT samples and 20000 complete windows; evolutionary maps allow at most two million cells. Work is limited by `windows × FFT samples × taper count ≤ 100 million`, using one taper for FFT estimates and the actual DPSS count for MTM. MTM has additional workspace limits. These are program ceilings, not recommended settings or performance guarantees.

Because a full-record FFT must accommodate the entire input, its input also cannot exceed the FFT-length ceiling. Cropping frequency does not reduce the work-budget product. If MTM reports unconverged windows, inspect the report and estimator settings rather than assuming reliable convergence.

References: [Evolutionary FFT](../reference/task_parameters_en.md#m-evolution_fft) · [Evolutionary MTM](../reference/task_parameters_en.md#m-evolution_mtm).

<a id="band-power"></a>

## Period-band power integration

Connect this task to a measured MTM, FFT or Welch PSD, an unnormalized evolutionary `mean_spectrum`, or a saved measured spectrum reused as a data source. Set both target bounds. The default reference is the published spectrum. To compare records, specify the same reference band. Both bands use the selected coordinate. On an age axis declared in ka, periods 90–130 correspond to frequencies 1/130 to 1/90 per ka. Undeclared axes retain their raw numerical scale.

The task integrates linear PSD on the frequency axis with trapezoids and linearly interpolated edge values. Bounds must lie within the published grid. The table gives effective frequency bounds, band and reference power, fraction and percent. A zero reference yields a diagnostic. The target width and its nonzero low-frequency limit must each reach one Rayleigh frequency. This uses the record length for FFT and MTM and the actual window length for Welch and evolutionary means; zero padding does not increase resolution. Window and MTM smoothing also affect peak separation. Power fractions are descriptive.
