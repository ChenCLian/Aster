# Using age models and cycle counting

These tasks integrate sedimentation rates, apply control tables or count cycles. They do not automatically search for an optimal age model. Start with the shared [coordinate and age-reference conventions](../concepts/chronology_coordinates_en.md).

<a id="rate"></a>
## Sedimentation rate to age

Input X is depth and Y is sedimentation rate at that location, not a proxy value. At least two samples are required, with strictly increasing X and finite positive rates in every row, including the last.

Choose depth, rate and age units, then set the age at the actual first depth. The default left-endpoint rule uses the left rate for each interval. With depth in metres and rate in cm/ka, the age increment is 100 × depth increment / left rate.

For example, depths 10, 11, 13 m, rates 2, 2, 999 cm/ka and first age 7 ka give ages 7, 57, 157 ka. The final rate is validated but is not used by the left-endpoint rule.

The linear-rate option assumes rate varies linearly with depth and integrates its reciprocal. It does not divide by the arithmetic mean of endpoint rates. Choose the rule from the meaning of the rate data, not from apparent smoothness.

The output is the depth–age curve `age_model`. A zero rate cannot encode a hiatus. See the [method definition](chronology.md) for integration and unit details.

<a id="apply"></a>
## Apply age model

For an existing model node, use [connected controls](#linked-controls). To date a depth-domain proxy from an external control-point table, use the file-based task below.

1. Select the control file and check delimiter, header and depth/age columns; defaults are columns 1 and 2.
2. Declare input depth units, control depth units, control age units and age reference. Units are not inferred from column headings.
3. Verify at least two controls and strictly increasing depth and age. Invalid rows are not silently skipped.
4. Check coverage of the whole input. Outside coverage is an error by default; linear extrapolation must be selected explicitly.
5. Inspect the age axis, controls and interval rates before deciding whether to resample.

| Output | Use |
| --- | --- |
| `aged_series` | Age X with original Y and unchanged sample count |
| `age_model` | Controls actually used in this execution |
| `age_intervals` | Control interval boundaries and rates for checking abrupt changes |

<a id="linked-controls"></a>

### Connect control points directly

Use the model node’s right-hand + to add **Apply age model from connected controls**. In **Manage pairs**, choose the depth-domain proxy for A and the model’s `age_model` output for B. The inputs may come from different steps or data trees. Explicitly pair a sedimentation-rate model with the appropriate proxy.

When attached to a cycle-count node, existing datasets are paired by identity with the proxy fed to the cycle counter. Choose the original unfiltered proxy instead when appropriate. Synthetic orbital sources can supply both their proxy and their true model. The task interpolates linearly between discrete depth–age controls and preserves the proxy values, model age unit and age reference. Declare the depth unit when the proxy has none. Coverage defaults to rejection; endpoint extrapolation is an explicit option.

Cycle markers usually lie inside the record. Check the first and last control depths before dating the full sequence. Select the covered proxy interval, or explicitly extend the cycle model to the input ends or enable linear extrapolation where justified.

External control tables can still be imported with **Apply age model**.

See [external-file conventions](../concepts/chronology_coordinates_en.md#files) for recalculation and saved results. Current limits are 4 MiB per control file, 100000 controls and one million input samples. Check content and necessary precision before simplifying a file; do not discard controls merely to bypass a diagnostic.

<a id="cycles"></a>
## Cycle-count age model

Use this with a geologically justified sequence of identifiable same-phase depth markers. The node does not filter, sort or resample automatically. Configure necessary preprocessing explicitly and inspect its effects.

Choose peaks or troughs and a cycle duration. Manual duration uses the selected age unit; model periods are converted from ka. The default manual duration is 41 ka and the first marker is assigned one full period, not zero. Select a specified first age for an absolute anchor or another origin.

Minimum local relief removes weak markers before minimum depth separation removes crowded ones. Relief uses neighbouring extrema and intervening valleys/peaks; it is not spectral-peak prominence. Separation is a depth distance, not a frequency-bin or age interval. A plateau uses its central sample, taking the earlier centre for even lengths. Plateaus touching the input boundary are not internal markers.

| Output | What to inspect |
| --- | --- |
| `selected_extrema` | Missing/extra markers or noise selected as a cycle |
| `cycle_anchors` | Original sample index, depth, value, assigned age and local relief |
| `age_model` | Exactly one period between adjacent same-phase markers |
| `interval_rates` | Rates implied by marker spacing and the fixed period |

At least two valid internal markers are required. The default model ends at the first and last markers, so it usually does not cover the input ends. Crop explicitly or justify extrapolation when applying it. Extend to input ends uses the age slopes of the first and last marker intervals.

A missed or extra marker changes assigned ages. Missing-cycle counts are not inferred. The appearance of the target period after tuning is not independent evidence that tuning is correct. A target-period envelope is not propagated into age uncertainty. See the [cycle method](cycle_tuning.md).

<a id="periods"></a>
## Milankovitch target periods

This is a standalone data source without input series. It calculates Waltham-model period envelopes at a specified geological age for target selection and comparison; it does not generate an astronomical phase curve.

Defaults are 300 Ma and obliquity; output periods use ka. The main table gives lower bound, midpoint and upper bound. The midpoint is their arithmetic mean, not a most-likely value, and the envelope is not a 95% confidence interval. Eccentricity components use a fixed period table, rather than changing with the entered age.

Cycle counting uses one fixed period at the specified age, and COCO model targets use the corresponding midpoints. Neither iterates geological age and periods along the record. Inspect the actual targets saved with results. See the [target-period definition](cycle_tuning.md) for formulas, sources and other outputs.
