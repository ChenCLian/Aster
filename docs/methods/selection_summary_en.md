# Interval selection and series summary

These tasks select samples or summarize Y values. They do not resample, repair missing values or estimate spectra. Use [sampling inspection](preprocessing_en.md#inspection-output) to check X ordering, duplicates or gaps.

<a id="interval"></a>
## Select or remove an interval

Specify lower and upper bounds in the current X coordinate, with lower ≤ upper. Both boundaries are included: Keep retains lower ≤ X ≤ upper, while Remove discards those samples.

Bounds use current X units. After an upstream age conversion, enter ages rather than original depths. The default 0–1 range is an initial value, not a recommended interval. Equal bounds match only samples exactly at that value.

Selection preserves existing order, X/Y values and axis metadata. It does not interpolate boundary points, reset the first X to zero or edit the disk file. A boundary between samples uses the nearest existing in-range sample. No remaining sample is an error.

Removing an internal interval leaves a real X gap; it does not concatenate the sides into a continuous regular record. Recheck sampling before downstream methods that require regular data. Do not fill a meaningful hiatus merely to make a task run.

Output `series` contains the selected samples; the report records operation, bounds and kept/removed counts. A plotted line may bridge the gap. A continuous line does not imply observations inside it.

<a id="summary"></a>
## Series summary

This task produces a one-row table summarizing the current Y values. Use it to compare count, central level and spread before and after processing. The default output is a table; absence of a curve is expected. Select `summary` in the output selector.

| Column | Definition |
| --- | --- |
| `count` | Number of input Y samples |
| `mean` | Arithmetic mean with equal weight per sample |
| `population_variance` | Population variance with denominator N, not N−1 |
| `minimum`, `maximum` | Minimum and maximum input Y |

At least one sample is required; a single sample has variance zero. X spacing is not used as a weight. Densely sampled intervals therefore contribute more to an irregular record's mean, which is not automatically a time average. Variance has squared Y units; mean and extrema retain Y units.

The output does not contain a median, confidence interval, standard error or sampling-quality assessment. Do not infer those from the five columns. To continue processing the series, branch from the upstream node; the summary table cannot feed a series-only task.

When comparing processing effects, check that both branches cover the same samples. A change in mean or variance can arise from interval selection or sample removal as well as transformation.
