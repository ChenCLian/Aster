# Data-processing effects and parameters

[Methods](../methods/README.md) · [中文](data_processing.md)

Choose the change you need before choosing a method. Smoothing, detrending, standardizing and outlier handling solve different problems; a smoother curve alone is not evidence of a better result.

<a id="effects"></a>
## Numerical processing versus display

These tasks produce new output series without overwriting the original file. Downstream tasks receive the selected output port. Changing a palette or display interpolation does not make the same numerical change.

| Operation | What changes | Interpretation to retain |
| --- | --- | --- |
| Smoothing | Y is estimated from neighbors | Real peaks and short cycles may be suppressed |
| Centering / detrending | A constant baseline or fitted trend is removed | Long-term change may itself be the signal of interest |
| Standardization | Y is expressed in standard-deviation units | Absolute amplitudes in original units are lost |
| Nonlinear transformation | Amplitude ratios and distributions change | Subsequent spectra describe the transformed series |
| Outlier handling | Selected points are marked, excluded or replaced | An extreme observation is not necessarily an error |
| Filtering | Frequency components receive different weights | A selected band does not establish the origin of a cycle |

There is no universal preprocessing chain. Keep an original branch, record each setting and compare before/after results; use the operations required by the research question.

<a id="neighborhoods"></a>
## Sample counts, fractions and physical spans

Moving averages, SG and Hampel use sample-count windows. LOWESS derives the neighborhood size from a fraction of the input count and weights by X distance. These settings are not fixed durations or depths.

For regular spacing, a W-point neighborhood spans `(W−1)×Δx` between its endpoints. With irregular sampling, physical span varies with position. A large gap does not automatically split the calculation. Split the record explicitly when a neighborhood should not cross a hiatus or sampling break. See [window definitions](spectral_parameters_en.md#windows) for analysis segments and tapers.

Larger neighborhoods usually smooth more strongly but mix longer intervals. Smaller neighborhoods can follow noise, leave trends in the residual or make outlier decisions unstable. Compare the target scale with the actual local sampling.

<a id="boundaries"></a>
## Endpoints and gaps

Moving averages shorten edge neighborhoods; SG fits the first/last full window; LOWESS uses asymmetric neighborhoods at the edges. Filters may extend or pad the record. Having an output value does not make an endpoint as reliable as the interior.

These tasks do not detect geological breaks or restore excluded samples. Exclusion may destroy uniform spacing. Before a task requiring regular sampling, [inspect the spacing](../methods/preprocessing_en.md#inspection), then choose segmentation or explicit resampling. Interpolation across a gap needs a substantive justification beyond making a task run.

<a id="units"></a>
## Units and amplitude comparisons

Smoothing, centering, detrending, band-pass, high/low-pass and Hilbert envelopes retain Y units. Z-scores and `ln/log10/asinh/tanh` transformations are dimensionless, with unit `1`. A filter response has dimensionless amplitude gain against frequency; it is not a time/depth series for continued processing on the original axis.

Scale-and-offset retains the existing unit label. If a factor implements a unit conversion, check the output labels and interpretation separately: multiplying ppm values by 0.001 does not automatically rename their unit.

Frequency has inverse X units. Power after standardizing no longer expresses differences in original-unit amplitudes. See [spectral units](spectral_parameters_en.md#power-units) for PSD and its display square root.

<a id="centering"></a>
## Centering, Z-scores and affine changes

- **Remove mean:** `y′=y−mean(y)`. Constants become zero. It does not remove a slope or curved trend; X and Y units remain unchanged.
- **Standardize:** `z=(y−mean(y))/s`. The default sample SD is `sqrt(sum((y−mean(y))²)/(N−1))`; population mode uses N. Sample mode needs at least two points; constants cannot be standardized in either mode.
- **Scale and offset:** multiply first, then add: `y′=a×y+b`. Negative a reverses the sign; zero a produces a constant. Interpret b in the numerical scale after multiplication. X is unchanged.

For Y=[1,2,3], centering gives [-1,0,1]. Sample Z-scores are also [-1,0,1], whereas population Z-scores are approximately [-1.224745,0,1.224745]. This reflects the SD convention.

Z-scores do not make a distribution Gaussian or remove outliers. They differ from [per-window maximum-power normalization](spectral_parameters_en.md#normalization). These three amplitude operations do not themselves require regularly spaced X and do not sort or resample the input.

Parameters: [Remove mean](../reference/task_parameters_en.md#m-demean) · [Standardize](../reference/task_parameters_en.md#m-standardize) · [Scale and offset](../reference/task_parameters_en.md#m-affine).

<a id="decisions"></a>
## Detection versus manual decisions

Detection proposes candidates; manual decisions express how particular points should be handled. The audit records both separately. Manual exclusion overrides automatic actions, including “Mark only”. Manual retention prevents automatic exclusion or replacement. Returning to detection removes that point's manual override.

See [outlier handling](../methods/outliers_and_transforms_en.md#outliers) for outputs and scope. Do not transfer point-specific decisions between different records.
