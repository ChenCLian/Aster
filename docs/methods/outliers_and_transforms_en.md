# Outlier handling and value transformations

[Shared concepts](../concepts/data_processing_en.md) · [中文](outliers_and_transforms.md)

<a id="outliers"></a>
## Detecting and handling outliers

Open Data processing → Cleanup → Outlier handling. Input must be finite with strictly increasing X and at least two samples; uniform spacing is not required. “Original” means the input entering this node, which may already reflect upstream processing.

- **Hampel:** detect local departures from a neighborhood median using its median absolute deviation (MAD).
- **Global MAD:** compare with the entire input's median and MAD. Trends or different regimes may be mistaken for outliers.
- **Bounds:** compare against lower/upper limits in input Y units. Values exactly at either bound are retained.

The MAD rule is `|y−median| > threshold×1.4826022185×MAD`, with default threshold 3. If MAD is zero, any departure from the median is detected. Hampel uses an odd sample-count neighborhood, shortened at endpoints; it does not infer missing sections or a fixed physical span.

Without manual exclusions, default “Mark only” leaves values unchanged. Automatic actions may instead exclude selected points or replace them with the median of the original local input neighborhood. Replacement is not iterative: already-replaced values do not affect later replacements. Exclusion does not fill gaps and may destroy uniform spacing. The original input remains available separately.

The node's preview context menu offers retain, exclude and return to detection for individual points. Manual exclusion deletes a point even under “Mark only” or median replacement; manual retention prevents automatic exclusion or replacement. Decisions belong to the current dataset and are not copied by batch rule application. Changes to input X/Y invalidate previous decisions and trigger a warning to review them again.

Start with marking and inspect candidates against measurement records and the research question. The threshold is used by Hampel and global MAD, not bounds. The local window is used for Hampel detection and median replacement under any detector. Thus global MAD detection can still use local replacement values.

The window must be odd, 3–1001 points; it may exceed input count, in which case neighborhoods are clipped to available samples. Window validity is checked even when the chosen operation does not use it. Lower must not exceed upper, including after switching detectors. Excluding every point fails rather than producing an empty series.

Detection is a screening aid, not a verdict that an observation is wrong. Real events can produce extreme values. Source files are not overwritten.

<a id="audit"></a>
## Outputs and point decisions

`series` is the processed output; `original` is the unchanged node input. `outliers` contains original values of points selected after manual overrides, which need not be deleted points. This output is omitted when none are selected. `outlier_audit` contains one row per input point; the sampling-inspection limit of 4096 detail rows does not apply here.

| Column | Meaning |
| --- | --- |
| `index` | Input position, starting at zero |
| `x`, `original_value` | Input X and Y |
| `result_value` | Result Y; excluded points use -1, interpreted together with `action` |
| `detected` | Automatic detector flag: 1 yes, 0 no |
| `selected` | Selection after manual decisions: 1 yes, 0 no |
| `action` | 0 retain, 1 exclude, 2 replace |
| `score` | Absolute departure divided by 1.4826×MAD; -1 when unavailable or unrepresentable |

A legitimate negative value is not a missing value merely because it equals -1. Read `action` before interpreting `result_value`.

Limits are 200000 input samples, 20 million sample visits for local-window processing and 5000 saved point decisions per input. Exceeding a budget fails explicitly rather than silently reducing the input.

<a id="transforms"></a>
## Value transformations

Open Data processing → Amplitude → Value transformation. Default is asinh with scale 1. Input X/Y must be finite; this task neither requires regular spacing nor sorts the data. Maximum input is one million samples. Scale s must be positive and have the input Y unit; X is unchanged and output is dimensionless.

| Function | Calculation | Domain and effect |
| --- | --- | --- |
| Natural logarithm | `ln(y/s)` | Every Y must be strictly positive; no automatic shift |
| Base-10 logarithm | `log10(y/s)` | Every Y must be strictly positive |
| Inverse hyperbolic sine | `asinh(y/s)` | Accepts negative, zero and positive values; nearly linear near zero and compresses large magnitudes |
| Hyperbolic tangent | `tanh(y/s)` | Mathematically in (-1,1); floating-point results may saturate at ±1 |

Choose the reference scale before checking the domain. For y=10 and s=10, ln gives 0 and asinh approximately 0.881374. Any zero or negative value causes either logarithm task to fail; such points are not silently skipped or shifted.

Do not add an arbitrary constant solely to make logarithms run: it changes ratios and interpretation. Asinh accepts signed inputs but does not repair data. Tanh saturation suppresses differences between extremes, which cannot be recovered from saturated outputs. Logarithmic color mapping changes only the display; these transformations change Y itself.

Transformations neither detect erroneous points nor fill gaps. They change amplitude ratios, distributions and subsequent spectral meaning. Record the function and scale and compare with the original series.

Parameters: [Outlier handling](../reference/task_parameters_en.md#m-outlier_handling) · [Value transformations](../reference/task_parameters_en.md#m-value_transform). See [current validation](../validation_current.md) for the tested scope.
