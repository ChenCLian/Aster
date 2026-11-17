# Filtering and amplitude envelopes

[Shared concepts](../concepts/data_processing_en.md) · [Methods](README.md) · [中文](filter_usage.md)

<a id="choose"></a>
## Choose the frequency range

| Goal | Task | Default output |
| --- | --- | --- |
| Retain lower-frequency variation | Butterworth low-pass | `filtered` |
| Suppress lower-frequency variation | Butterworth high-pass | `filtered` |
| Extract a band and its envelope | Gaussian / Taner band-pass and envelope | `filtered`, with `envelope` also available |
| Compute the analytic-signal magnitude of an existing series | Hilbert amplitude envelope | `envelope` |

All require finite, strictly increasing, regularly spaced X/Y, without automatic resampling. Frequency uses inverse X units: a 100 ka period is 0.01 /ka, not a frequency setting of 100. Nyquist is `1/(2×Δx)`. Default frequencies are starting values, not detected cycles.

Choose the band from the research question and original spectrum, then inspect the output and response. Filtering an oscillation does not establish orbital forcing or statistical significance.

<a id="butterworth"></a>
## Butterworth low-pass and high-pass

Defaults are single-pass cutoff 0.05 and order 4; order may be 1–10. Cutoff/Nyquist must lie within 0.0001–0.9999, avoiding extremes near zero and Nyquist.

The filter runs forward and backward. Settings define a single-pass filter: amplitude gain at the specified cutoff is `1/√2` for one pass but **0.5** for two passes, giving a power ratio of 0.25. `filter_response` shows the combined amplitude gain. It is not a single-pass half-power response. Higher order sharpens the transition but can worsen finite-record boundary behavior.

Each end uses `3×(order+1)` points of odd reflection. The input count must exceed this padding length: default order 4 needs at least 16 points. The result is cropped to the original count and retains X and Y units. Zero phase does not eliminate endpoint distortion and is not equivalent to real-time causal filtering.

No preliminary demeaning is performed. Low-pass DC gain is 1; high-pass DC gain is 0. Preserving a constant baseline does not guarantee that every finite record retains exactly the same sample mean. Maximum input is one million samples.

[Low-pass parameters](../reference/task_parameters_en.md#m-butterworth_lowpass) · [High-pass parameters](../reference/task_parameters_en.md#m-butterworth_highpass).

<a id="bandpass"></a>
## Gaussian and Taner band-pass

At least eight samples are required. Defaults are lower 0.02 and upper 0.06, with `0<lower<upper<Nyquist`. Both remove the input mean before applying the frequency-domain response, without adding it back. Constants produce zero filtered values and zero envelopes.

Band edges are half-power frequencies: amplitude gain `1/√2`, power ratio 1/2; center gain is 1. They are not hard cutoffs, and out-of-band response may remain nonzero.

- **Gaussian:** a gradual response; trailing zero padding to `10×the smallest power of two ≥N`, followed by inverse transformation and retention of the first N samples.
- **Taner:** an N-point periodic DFT. Roll-off q defaults to 12 and ranges from 0.001 to 20. It controls transition shape, not dB/octave or Butterworth order.

Matching bands does not make endpoint behavior identical. Taner joins the ends through periodic continuation; Gaussian padding also leaves boundary effects. Compare endpoints and the number of band-limited cycles within the record.

Both tasks return `filtered`, `envelope` and `filter_response`. The envelope is computed from the filtered N-point series, so an extra Hilbert task is normally unnecessary. Check downstream port selection: the envelope and frequency-axis response are different from the filtered series.

The limits are one million input samples and 4194304 FFT points. Gaussian padding implies a smaller effective input limit, N≤262144: the next sample triggers the next padding size and exceeds the FFT budget. An oversized input fails rather than being silently shortened.

[Gaussian parameters](../reference/task_parameters_en.md#m-gaussian_filter) · [Taner parameters](../reference/task_parameters_en.md#m-taner_filter) · [Formulas and reference version](filtering.md#数学和参考版本).

<a id="hilbert"></a>
## Standalone Hilbert envelope

Input may contain 2–1000000 samples. The task uses the selected upstream series without automatically adding a band-pass filter. Demeaning is optional and disabled by default. A constant of 3 has envelope 3, or zero when demeaning is enabled.

`quadrature` is the Hilbert quadrature component; `envelope=sqrt(y_work²+quadrature²)`, where `y_work` reflects the demeaning choice. The additional `signal` output stores the exact analysed `y_work`; all three retain the input axis names and units. The N-point finite-record analytic signal has periodic boundary effects.

Plots show the envelope and analysed signal together by default; select quadrature separately when needed. Without demeaning, the analysed signal equals the selected upstream data. With demeaning, it is the mean-removed series. Gaussian/Taner plots pair the envelope with the filtered series. Existing records are not recomputed or amended: overlay their curves manually, or rerun the task to obtain the new default combination.

For a narrow-band, relatively single-component oscillation, its envelope can describe amplitude variation. A broadband or multicomponent envelope is computable but is not automatically the unique instantaneous amplitude of one physical process. It differs from square-root PSD display and supplies no p-value or significance threshold.

[Parameters](../reference/task_parameters_en.md#m-hilbert_envelope) · [Units](../concepts/data_processing_en.md#units).

<a id="outputs"></a>
## Outputs and troubleshooting

`filter_response` plots dimensionless amplitude gain against frequency, not PSD. Inspect passband, cutoff gain and transition before interpreting the original/filtered series and envelope. Display smoothing cannot correct a filtering result.

| Symptom | Check |
| --- | --- |
| Invalid cutoff or band | X units, period-to-frequency conversion, spacing and Nyquist limits |
| Record too short | Order-dependent Butterworth padding or band-pass/Hilbert minimum count |
| Filtering fails after outlier exclusion | Whether spacing remains regular; justify segmentation or resampling |
| Strong edge deviations | Endpoint discontinuities, boundary convention and cutoff period relative to record length |
| Unexpected envelope | Selected output port, demeaning and multiple components |
| Different filters disagree | Actual responses, DC handling and boundaries, not just frequency settings |

This describes implemented behavior, not universally optimal parameters. See [filter conventions](filtering.md), [extended methods](spectral_extension.md) and [current validation scope](../validation_current.md).
