# Spectral results and statistical interpretation

[Task guide](../methods/spectral_inference_en.md) · [Methods](../methods/README.md) · [中文](statistical_interpretation.md)

<a id="outputs"></a>
## Identify the reported quantity

| Output | What it describes | Not directly equivalent to |
| --- | --- | --- |
| FFT / Welch / MTM PSD | Distribution of variation over frequency | Signal amplitude or probability that a cycle exists |
| Normalized Lomb–Scargle power | Improvement from a sine/cosine fit with an intercept | PSD, p-value or false-alarm probability |
| MTM peak table | Local peaks passing geometric filters | Confirmed astronomical cycles |
| Magnitude-squared coherence | Frequency-related structure under the chosen estimator | Significance probability or causation |
| Wavelet power map | Power across position and period | A globally significant map |
| AR1 global p-value | Compatibility of one input with the stated AR1 null | Probability that each peak is a real signal |

Check units, normalization and color mapping separately. Brightness and smoothness cannot replace numerical and statistical definitions. See [spectral units and display](spectral_parameters_en.md#power-units).

<a id="local-global"></a>
## Local thresholds and global tests

A local threshold compares one frequency or time-frequency location. A global test addresses a predefined collection or record. Searching many locations and selecting the largest peak changes the question: a pointwise 95% curve does not imply a 5% false-positive probability for the entire record.

Ordinary MTM AR1/SWA curves and CWT nominal 95% ratios use approximate background and distribution conventions. Background estimation, finite length, correlated bins, preprocessing and boundaries can affect actual error rates. Correct numerical thresholds and calibrated statistical error rates are different validation targets.

The AR1 global test uses independent reference simulations and a predetermined parameter set for **one incoming series over positive interior Fourier bins**. DC is excluded; for even N, Nyquist is also excluded. It does not control selection across files, methods or repeated settings. Rejecting the null does not identify a particular peak or physical cause.

<a id="fdr"></a>
## FDR and peak detection

FDR is the expected false-discovery proportion under the relevant procedure's assumptions. It is neither the probability that each selected peak is false nor a single-bin false-positive probability.

Ordinary MTM follows the recorded reference's bandwidth-dependence correction and fitted SWA background. **Actual FDR control has not been established for fitted backgrounds.** Existing calibration conditions have shown excess false discoveries. Treat q=0.05 as a nominal reference under this convention, not a guarantee of 5% actual FDR. Curves for different q may coincide without an implementation error. See [statistical limits and evidence](../methods/statistical_limits.md).

Prominence, height, width and distance are geometric filters. Relaxing them adds candidates without strengthening statistical evidence. Bin-based widths and distances change their physical frequency meaning when FFT padding changes; compare frequency grids as well as settings.

<a id="background"></a>
## Background assumptions and preprocessing

AR1 is a noise hypothesis, not a universal model for geological backgrounds. Trends, hiatuses, nonstationarity, non-Gaussian noise and processing chains affect its applicability. Passing finite-value and regular-spacing checks does not establish statistical assumptions.

External selection, detrending, smoothing, filtering and resampling are not replayed in AR1 global reference simulations. Only its internal LOWESS switch applies the same processing to observations and references. Repeatedly changing bands, windows or seeds until a result is significant falls outside a single prespecified test.

Disabling internal LOWESS by default is not a recommendation to avoid all detrending. Choose processing from the research problem, and retain input provenance, preprocessing, method version, settings, frequency domain and random seed in the report.

<a id="unavailable"></a>
## Not passed, not evaluated and inconclusive

| Situation | Interpretation |
| --- | --- |
| MTM peak flags 1 / 0 / −1 | Passed / not passed / not evaluated or unavailable; −1 is not “nonsignificant” |
| MTM background fit fails | The main spectrum may remain usable; corresponding threshold ports are absent; inspect warnings |
| CWT significance ratio absent | It may be disabled, variance may be zero, or background requirements may fail; inspect the report |
| Cross-spectrum coherence/phase shown as “—” | Undefined, not a valid zero; CSV cells remain empty |
| AR1 `test_available=0` | Empty candidate parameter set: inconclusive, conservatively stored as p=1; not negative evidence |
| Task failure | Input, parameter, resource or numerical problem; no interpretable test conclusion |

Successful computation and rejection of a null hypothesis are different states. Non-rejection does not prove absence of cycles; power or scale/model mismatch may matter.

<a id="coi"></a>
## Wavelet boundaries and the cone of influence

COI follows a mother-wavelet boundary-decay convention. Periods greater than the local COI period are edge-affected; their values remain in the output without automatic masking. COI is not a confidence interval, uniform error bound or verdict that all exterior features are false.

Axis reversal, logarithmic coordinates and palettes do not change this rule. Long-period bright regions near endpoints need boundary and background checks even when their local ratio exceeds 1. See [wavelet outputs](../methods/spectral_inference_en.md#wavelet).
