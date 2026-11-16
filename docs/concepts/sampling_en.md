# Sampling, duplicate coordinates and interpolation

[Documentation index](../README.md) · [Task instructions](../methods/preprocessing_en.md) · [中文](sampling.md)

This page explains concepts shared by several tasks. Defaults, allowed ranges and available methods remain specific to each task's parameter reference.

<a id="coordinates"></a>
## Establish what X represents

X may represent time, age or depth; Y is the corresponding observation. A sampling interval is the difference between adjacent X values, in the same units as X. Editing a unit label does not convert years to kiloyears or change coordinate values.

Strictly increasing means that every X is greater than its predecessor. Uniform sampling additionally requires adjacent intervals to agree within the allowed numerical tolerance. A smooth-looking curve need not have uniform sampling. Requirements depend on the downstream method: FFT and evolutionary FFT require uniform sampling, but this does not mean every analysis must start with resampling.

If ages decrease along the recorded sequence, sorting changes that order. Confirm the coordinate direction and its interpretation first. Sorting moves paired X/Y samples together.

<a id="duplicates"></a>
## Duplicate X is different from repeated Y

Duplicate coordinates have exactly equal X values. Equal Y values at different locations are not duplicate coordinates; nearly equal X values are not merged automatically.

| Policy | Operation at one X | Check before choosing |
| --- | --- | --- |
| Average | Combine the associated Y values into one observation | Whether these measurements can reasonably represent the same location |
| First / last Y | Keep one value in the original order received by this node | Whether that order has an intended meaning |
| Error | Stop when a duplicate coordinate is found | Whether source records should be reviewed before deciding how to combine them |

For `(X,Y)=(2,20),(1,10),(1,14)`, sorting and averaging gives `(1,12),(2,20)`. Keeping the first or last value gives Y=10 or Y=14 at X=1, respectively.

First does not mean the smallest Y; last does not mean the largest Y. Input order refers to the sequence received by the current node, which upstream operations may already have changed.

<a id="spacing"></a>
## Sampling interval and grid density

Resampling defines new X coordinates and then interpolates Y at those positions. Smaller intervals usually produce more output samples, but do not add independent observations.

- **No resampling:** retain the cleaned sampling locations. In Sort, deduplicate and resample, sorting and duplicate handling still run.
- **Mean / median:** use a representative interval from adjacent cleaned coordinates. The median is less affected by a few large intervals, but is not universally suitable.
- **Minimum:** use the closest spacing. One unusually close pair may greatly increase output size.
- **Maximum:** use the widest spacing. Short-scale variation may be poorly retained.
- **Specified interval:** enter an explicit value in X units for reproducibility or downstream sampling requirements.

If X is in ka, an interval of 0.1 means 0.1 ka. Changing the label to ka while leaving numerical coordinates in years gives an incorrect interpretation of both spacing and frequency.

Coarser resampling is not automatic anti-alias filtering. Sort, deduplicate and resample does not insert a low-pass filter. Before reducing the sampling density, consider the target periods and high-frequency content when deciding whether filtering is required. Finer interpolation cannot recover high-frequency information absent from the original observations.

<a id="interpolation"></a>
## What the interpolation method changes

| Method | Meaning | What to inspect |
| --- | --- | --- |
| Linear | Estimate Y along straight lines between neighboring samples | Transparent local behavior, with possible slope discontinuities at samples |
| Natural cubic spline | Join cubic polynomials smoothly, with zero second derivative at the endpoints | Smoothness does not establish accuracy; local overshoot is possible |
| Akima | Construct piecewise cubic interpolation from local slopes | Still an estimate, not guaranteed to be monotone or a substitute for outlier treatment |

Inspect peaks, endpoints and long gaps for implausible shapes. Minimum sample counts, endpoint handling and extrapolation rules are task-specific; do not apply one task's boundary rules to every interpolation task.

<a id="gaps"></a>
## Large intervals and missing sections

A large interval may reflect irregular sampling, removed points, missing entries or a genuine gap in the record. A flag identifies a location to inspect. It does not establish a sedimentary hiatus or automatically split a record, repair an age model or reconstruct a missing process.

Interpolation may generate values between the two sides of a gap, but those values are not observations of the missing interval. Sort, deduplicate and resample does not automatically forbid interpolation across large gaps. If a gap must not be crossed, identify continuous sections from the research context and analyze them separately. Removing interior points alone does not prevent later interpolation between the remaining endpoints.

When the input is unordered, address ordering and duplicate coordinates before interpreting the interval distribution. Sampling inspection uses the original adjacent order received by the node; repeating it after sorting may change the results.

<a id="limits"></a>
## Distinguish three types of size limit

| Level | Meaning | Can export recover more? |
| --- | --- | --- |
| Accepted calculation input | Determines whether the task accepts the input; excessive size causes an error | Export cannot bypass a calculation limit |
| Detail rows retained by a task | The full input is inspected, but only a bounded number of detail rows is retained | Export contains only the retained rows |
| Preview display budget | Existing output is reduced for quick display | Export from a saved result can access its actual output, but cannot recover details never retained by the task |

See [inspection outputs](../methods/preprocessing_en.md#inspection-output) for task-specific limits and report fields. Preview display budgets are documented in [Live preview (Chinese)](../ui/live_preview.md#display-limits).
