# COCO: targets, scans and interpretation

COCO compares the input spatial spectrum with the spectral shape of fixed target periods under candidate sedimentation rates. It helps compare rates; it does not directly build an age model or establish astronomical causation.

<a id="inputs"></a>
## Prepare the input

Use a strictly increasing, regularly sampled depth–proxy series. The node does not clean, interpolate or remove a LOWESS background automatically. Check sampling and [depth units](../concepts/chronology_coordinates_en.md#units). Relabelling an age series as depth is not a conversion.

Run a fixed-target scan first to inspect usable candidates, the frequency band and targets before deciding on Monte Carlo testing. Upstream filtering and interpolation affect spectra and noise structure and must be considered in interpretation.

<a id="targets"></a>
## Target periods, powers and comparison range

The default manual periods, 405, 125, 95, 41, 24, 22 and 19 ka, are near-modern examples, not universal settings for all geological ages. Model targets use period midpoints at the specified age. They supply periods, not actual astronomical phases.

Weights are relative powers, not sinusoidal amplitudes. Defaults 1, 1, 1, 0.64, 0.36, 0.36, 0.36 correspond to the seven targets in order. Check weight count and meaning after changing the period list.

The default rate range is 0.5–20 cm/ka with step 0.1. Candidates follow minimum + integer × step; an off-grid maximum is not appended. Use geological evidence to set the range. Finer spacing does not create independent evidence.

The default comparison band is (0, 0.06] 1/ka, excluding DC, and must include every target strictly below its upper limit. Each candidate must also place targets at or above Rayleigh and strictly below Nyquist, keep the whole band below or at Nyquist, and supply at least eight comparison bins. Padding densifies the grid without resolving insufficient record length. See [FFT padding](../concepts/spectral_parameters_en.md#fft-padding).

<a id="scan"></a>
## Fixed-target scan: inspect diagnostics before the maximum

Values are Pearson correlations and can be negative. The target averages sine/cosine powers over phase; it is not a particular orbital solution summed at an arbitrary phase.

| Output | Meaning |
| --- | --- |
| `correlation` | Raw correlations for valid candidates |
| `scan_diagnostics` | Full grid, availability flags, exclusion reasons and band diagnostics |
| `best_observed`, `best_target` | Spectra at the best valid candidate, each normalized to its own maximum |
| `target_periods` | Actual target periods, frequencies and relative powers |

A zero correlation placeholder in an unavailable diagnostic row is not an observed zero correlation; always inspect availability. Ties select the smaller rate. Independently normalized best spectra compare shape, not absolute power.

The scan supplies no p value, and its maximum is not a rate confidence interval. See the [fixed-scan definition](coco_scan.md) for formulas and status codes.

<a id="monte-carlo"></a>
## Monte Carlo: declare a model and choose the appropriate p value

The default null is fixed-parameter Gaussian AR1 with rho=0, i.e. white noise. The program does not fit rho from the input. Estimating rho from the same record before entering it produces a plug-in simulation, not the independently specified simple-null setting. Upstream preprocessing is not replayed in simulations.

Random spectral peaks provide an alternative reference, not a complete time-series noise model. Automatic peak count also comes from the observed spectrum. Its outputs are reference p values and do not guarantee false-positive control under arbitrary red noise. See [background models](../concepts/statistical_interpretation_en.md#background).

| Output | Question addressed |
| --- | --- |
| `pointwise_p` | Evidence at a candidate rate specified in advance |
| `scan_adjusted_p` | Positive-correlation evidence accounting for this fixed rate scan |
| Global p in `mc_summary` | Evidence for the largest positive correlation in this scan |
| `null_maxima` | Maximum positive correlation from each simulation for inspection |

After searching for the maximum, use global or scan-adjusted p. Adjustment does not cover repeated changes to periods, weights, bands or rate ranges, selection across files, or the whole sliding-window map. A p value is not the probability that a rate is true.

Defaults are B=199, seed 1729 and alpha=0.05. Nonzero rank counting gives minimum p=1/(B+1), or 0.005 for 199 simulations. Alpha cannot be below this resolution. More simulations improve resolution but need not lower p or correct a wrong model. A fixed seed supports reproducibility; do not search seeds for a smaller p.

Only positive correlation contributes evidence. No positive match gives p=1; an entire curve of ones need not indicate a fault. Raw correlations still retain negative values. B can be 19–20000, subject to additional workload limits for large inputs or dense grids. Follow the size diagnostic; the count limit does not guarantee every configuration will run. See the [Monte Carlo and eCOCO definition](coco_analysis.md).

<a id="evolution"></a>
## Sliding-window eCOCO: read the local matching map

Defaults are a 512-sample window and 128-sample step. Only full windows are used, with at least two windows and two usable rates. Windows must resolve the selected targets. An incomplete tail is not padded; its sample count is recorded.

Map X is the midpoint of actual endpoint depths, Y is cm/ka, and colour is raw correlation without additional per-column normalization. The best-rate line selects each window independently and may jump. There is no path smoothing or automatic age-model construction.

`window_diagnostics` records actual boundaries and the best candidate; `rate_diagnostics` records shared rate availability. Inspect `best_correlation` to distinguish a strong match from merely the largest of several weak matches.

eCOCO supplies no significance test. Overlapping windows and neighbouring rates are correlated; a continuous band is not repeated independent evidence. A zero-variance window produces a diagnostic with its location rather than being silently skipped or painted as zero correlation. Limits include 512 windows and two million cells, plus workload limits.
