# Sampling inspection, cleanup and resampling

[Shared concepts](../concepts/sampling_en.md) · [Methods (Chinese)](README.md) · [中文](preprocessing.md)

These three tasks solve different problems. Inspect the input and choose the necessary operations; adding all three is not required.

| Goal | Task | Effect on the downstream series |
| --- | --- | --- |
| Inspect order, duplicate coordinates and intervals | Sampling and gap inspection | Preserves the input |
| Sort and resolve duplicate X values only | Sort and deduplicate | May change order, Y and sample count; no interpolation |
| Create a regular grid after cleanup | Sort, deduplicate and resample | Changes sampling locations and estimates Y by interpolation |

<a id="inspection"></a>
## Sampling and gap inspection

### When to use it

Inspect new input or data after removing points or selecting intervals. This task accepts unordered coordinates; downstream spectral tasks may impose stricter requirements. Inspection does not sort, deduplicate, repair gaps or automatically discard invalid values.

### Choosing the gap factor

The default factor is 3. The task takes the median of positive differences between adjacent X values in the current input, then flags intervals **strictly greater than median × factor**. Equality does not trigger a flag. A larger factor flags fewer gaps; a smaller factor flags more. Interpret the threshold in the sampling context, rather than as a sedimentary-hiatus criterion.

For X=`0, 1, 2, 8`, the positive intervals are `1, 1, 6` and their median is 1. A factor of 3 flags the interval from 2 to 8. Negative intervals do not enter this median; inspect ordering before deciding whether to sort and repeat the check. When no positive interval exists, the report marks positive-spacing statistics unavailable. No flagged gap does not imply valid sampling.

<a id="inspection-output"></a>
### Reading the outputs

Read the summary first, then use details to locate problems.

| Output | Content | Interpretation |
| --- | --- | --- |
| Original series `series` | Unchanged input X/Y | Downstream input requirements still apply |
| `sampling_summary` | Samples, duplicate X, descending/equal adjacent pairs, increasing/uniform flags and gap count | Covers the full input; Boolean fields use 1 for true and 0 for false |
| `sampling_intervals` | Left/right X and spacing for original adjacent pairs | Retains the first 4096 intervals in input order at most |
| `sampling_gaps` | Pairs exceeding the threshold | Retains the first 4096 flagged gaps in input order, not the 4096 largest gaps |

`right_point_1based` is the right sample's position in this node's input, starting at 1. Headers, skipped invalid rows and upstream processing mean it need not equal the original file's row number.

Duplicate X counts the excess samples beyond one per group of equal coordinates, including nonadjacent duplicates. Descending/equal adjacent pairs are counted in current input order. Uniformity is checked only for strictly increasing coordinates: the maximum relative interval deviation from the mean must be at most `1e-6`. This numerical tolerance is not an assessment of measurement uncertainty.

**The 4096-row limit affects retained details, not full-input statistics or the original series.** An input of 5000 samples has 4999 adjacent intervals. Its interval table retains the first 4096 rows while the summary covers all 5000 samples. Exporting that table does not regenerate omitted details.

The report fields `interval_details_truncated` and `gap_details_truncated` indicate truncation separately. The preview table also has its own display budget, so fewer rows may appear on screen than exist in the output. See [three types of size limit](../concepts/sampling_en.md#limits). Inspection accepts at most one million samples; larger input is rejected rather than partially inspected.

<a id="cleanup"></a>
## Sort and deduplicate

Use this task to clean ordering while retaining irregular sampling positions. Paired X/Y samples are sorted by increasing X, then exactly equal X values are handled according to the chosen policy. Units are preserved and no new grid is created.

The default averages Y values at duplicate X. Other policies keep the first value, keep the last or fail on duplicates. See [duplicate coordinates](../concepts/sampling_en.md#duplicates) for meanings and an example. At least two finite X/Y samples are required, with at least two distinct X values after cleanup. Handle missing values through the import policy first; cleanup does not silently remove them.

Parameter reference: [Sort and deduplicate](../reference/task_parameters_en.md#m-clean_series).

<a id="resampling"></a>
## Sort, deduplicate and resample

### Suggested setup order

1. Confirm numerical X values and units, and identify gaps that must not be bridged.
2. Select a duplicate-X policy. Sorting and duplicate handling always run first.
3. Choose the spacing mode. The default uses the median adjacent interval after cleanup. With no resampling, processing ends after cleanup and interpolation settings are unused.
4. For an explicit spacing, choose fixed/specified interval and enter a value in X units. That value is unused in the other modes.
5. Select interpolation, inspect the output curve and, if needed, repeat sampling inspection before methods requiring uniform input.

See [spacing and interpolation](../concepts/sampling_en.md#spacing) for shared selection guidance. This task defaults to linear interpolation; natural cubic spline and Akima are also available. After deduplication, resampling requires at least 2 distinct X values for linear, 3 for natural cubic spline and 5 for Akima. Insufficient input causes an error, not an automatic switch to linear. No-resampling mode skips interpolation sample-count checks, but cleanup still requires at least two distinct X values.

<a id="grid-endpoints"></a>
### Why the last coordinate may change

The output grid starts at the smallest cleaned X and advances by the chosen spacing. It does not extrapolate beyond the input or append an irregularly spaced final point. The largest X is retained only when it lies on the grid, allowing for floating-point rounding.

For a range from 0 to 10 and spacing 3, output X is `0, 3, 6, 9`; 10 is not appended. Spacing 2 gives `0, 2, 4, 6, 8, 10`. Original interior sample positions need not be retained either. X/Y units are preserved, but locations and sample count may change.

<a id="resampling-failures"></a>
### Diagnosing errors or unexpected shapes

By default the node resamples using the selected settings. A gap covering at least half the span and 1,000 grid intervals produces a warning with both X endpoints in the report. Turn off “Allow interpolation across an extreme gap” to stop before interpolation if one gap covers at least 90% of the span and 10,000 grid intervals. Previously saved choices are honored. The grid limit is one million points.

| Symptom | What to inspect |
| --- | --- |
| Duplicate X or too few distinct coordinates | Review source records and duplicate policy; identical X values cannot define a resampling interval |
| Invalid grid or excessive output size | Check units and spacing; spacing larger than the span cannot produce two points, while very small spacing may exceed the budget |
| Spacing cannot be represented accurately | Avoid tiny intervals on very large numerical coordinates; convert coordinates and units consistently when appropriate |
| Too few samples for Akima or cubic interpolation | Check distinct X after cleanup; explicitly select linear if suitable, or supply enough observations |
| A continuous curve across a long gap | Interpolation bridged the gap; identify continuous sections rather than treating estimates as new observations |
| Implausible shapes near peaks | Compare interpolation, spacing and original samples; retain original sampling positions when appropriate |

The current grid requires `(maximum X − minimum X) / spacing` between 1 and 999999, producing at most one million points. Very small spacing may also fail floating-point resolution checks. A failed task supplies no valid downstream output. Its successful report records input/output counts, merged rows and the actual spacing when resampling runs.

See the [parameter reference](../reference/task_parameters_en.md#m-preprocess) for defaults and exact ranges, and the [user guide](../USER_GUIDE_EN.md#preview) for preview versus saved results.

Mean removal, standardization, linear detrending and LOWESS calculate deviations before restoring a large common baseline. Residuals retain the small differences resolved in the original observations. The node reports a diagnostic when its output exceeds the numeric range. Moving average and Savitzky–Golay smoothing use the amplitude range of each window, so a spike outside a window leaves its result unchanged.
