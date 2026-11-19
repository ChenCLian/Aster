# Smoothing and detrending

[Shared concepts](../concepts/data_processing_en.md) · [Methods](README.md) · [中文](smoothing_trends.md)

Smoothing returns a neighborhood estimate. Detrending offers separate fitted-background and residual outputs. Decide which component you want and check the output port connected downstream.

<a id="moving-average"></a>
## Moving average

A simple local mean can suppress rapid fluctuations. Input must be finite with strictly increasing X and at least two points. Irregular X is allowed, but averaging counts neighboring samples, not elapsed time; no resampling is performed.

The default is 11 points. The window must be odd, within 1–501 and no larger than the input. A one-point window preserves the values. Interior neighborhoods are centered; endpoint neighborhoods are shortened. For Y=[1,2,3,4,5] and window 3, the result is [1.5,2,3,4,4.5]. There is no zero padding or removal of edge outputs.

The `series` output retains X, sample count and units, but its mean, peaks and frequency components may change. Irregular inputs produce a warning about varying physical window span. [Choosing a neighborhood](../concepts/data_processing_en.md#neighborhoods) · [Parameters](../reference/task_parameters_en.md#m-moving_average).

<a id="savitzky-golay"></a>
## Savitzky–Golay smoothing

A local polynomial is fitted and evaluated at the current sample. Input must be finite, strictly increasing and regularly spaced, with at least two points. There is no implicit interpolation. Defaults are 11 points and degree 2. The window must be odd, no larger than the input or 501; degree is 0–5 and strictly less than the window count.

Interior windows are centered. Left-edge values use a fit over the first full window, evaluated at each corresponding position; right-edge values use the last full window. This differs from the moving average's shortened edge windows, including when SG degree is zero. Window 1 is valid only with degree zero.

A higher degree is not simply greater accuracy: it may retain more local variation or cause overshoot and negative values. SG is not constrained to a neighborhood's value range. It does not output derivatives or confidence bands. X, sample count and units are unchanged. [Parameters](../reference/task_parameters_en.md#m-savitzky_golay).

<a id="linear"></a>
## Linear detrending

Fit an ordinary least-squares line with an intercept against the actual X values, then subtract it from Y. At least two points and nonconstant X are required; uniform spacing is not. The task does not sort, resample or automatically reject extremes.

The default `detrended` output contains residuals; `trend` contains the fitted line. Both retain X and Y units. Their pointwise sum reconstructs Y within floating-point error. The report records slope and intercept. There is no retain-mean switch: the constant baseline and slope are both removed.

Strong extremes can influence the fit. Reduced low-frequency power after detrending is partly a consequence of the operation, not proof that the original record lacked long-term change. [Parameters and outputs](../reference/task_parameters_en.md#m-linear_detrend).

<a id="lowess"></a>
## LOWESS detrending

With positive robust iterations, the estimated work is the number of samples times neighbors per window times the number of fitting passes (robust iterations plus one). More than one billion neighborhood terms stops the node before fitting. Ordinary LOWESS has its own evaluation path.

Input must be finite with strictly increasing X and at least two points; irregular spacing is allowed. Local linear fits are used. Defaults are fraction 0.35, zero robust iterations and retain-mean disabled.

The fraction ranges from 0.001 to 1. The base neighborhood count is `max(2,min(N,floor(fraction×N)))`, with weights based on X distances. This is not a percentage of physical X span: 35% of 101 points gives a base neighborhood of 35 points. Edge neighborhoods are asymmetric and do not automatically avoid large gaps. Very small fractions may closely follow individual observations and leave near-zero residuals.

Zero iterations means ordinary LOWESS. Values 1–10 enable residual-based robust reweighting; 5 is an optional rLOWESS setting. Reweighting reduces the influence of some large-residual points; it neither deletes points nor establishes measurement errors. More iterations cost more and should not be increased simply to make the curve smoother.

By default, `detrended=y−trend`, with the fitted background in `trend`. LOWESS residuals need not have exactly zero mean. Retain-mean mode instead gives `y−trend+mean(trend)`, preserving the mean of input Y. It does not simply add `mean(y)`. The trend output is unchanged; `mean_adjustment` reports the added value.

Both outputs retain X, count and Y units. With retain-mean enabled, subtract the adjustment from “residual + trend” to reconstruct Y. [Parameters](../reference/task_parameters_en.md#m-detrend_lowess) · [Units and standardization](../concepts/data_processing_en.md#units).

<a id="selection"></a>

For long series, ordinary LOWESS reuses partial sums across neighbouring windows and fits every input coordinate. The report records the evaluation method in `fit_evaluation`. The running task displays fitting progress and accepts cancellation during the fit.

## Choosing and checking

| Goal or problem | What to check |
| --- | --- |
| Obtain locally smoothed values | Moving average or SG; compare actual window span and peak changes |
| Remove a slow background | Linear or LOWESS; inspect both `trend` and `detrended` |
| Fewer than 11 input samples | Reduce the window explicitly; also check SG degree |
| SG rejects irregular spacing | Inspect X and justify any resampling; moving average is not an equivalent replacement |
| Endpoint bias or overshoot | Compare boundary rules, neighborhoods and original values |
| Almost no variation after detrending | Check the selected output, a very small LOWESS fraction, and whether the trend contains the signal of interest |

Moving average and SG allow at most one million samples, with `N×window≤100 million` work terms. These are limits, not recommended settings. LOWESS fits at each position; fraction and robust iterations affect its cost, and the smoothing-window budget should not be applied to it. Exact defaults remain in the generated parameter reference.

These tasks provide neither significance tests nor confidence intervals or an optimal window. See [existing numerical conventions](smoothing_spectra.md) and [current validation scope](../validation_current.md).
