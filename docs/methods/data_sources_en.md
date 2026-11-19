# Files, saved results and test sources

Choose an entry point by data origin, then check X, Y, units and sampling. The output type determines compatible downstream tasks; similar names do not make different types interchangeable.

<a id="files"></a>
## Start from a text file

Import delimited CSV, TXT, TSV or DAT files in Input data. A file may have many columns, but each series uses one X column and one Y column. Check delimiter and header before choosing columns and units. Displayed column numbers start at 1.

Completely blank lines are ignored. An empty field, NaN, NA, N/A or NULL in a selected X or Y column denotes a missing value. These text markers are case-insensitive. Other unparseable text and infinities are invalid. By default the reader reports the line. Enabling Skip invalid rows skips and counts these rows. The report gives the total in `skipped_rows` and the subset skipped solely for missing values in `missing_rows`. All finite numbers are read at their original values.

Unit strings label the data without converting numbers. Reading and processing do not overwrite the source. Click Recalculate after externally editing a file; the program does not watch filesystem changes.

Bulk edits validate the current column positions for each selected file; they do not match columns by header names. Incompatible files retain their settings with reasons. See [input-selection scope](../USER_GUIDE_EN.md#input-selection) for the distinction from node parameters.

<a id="database"></a>
## Continue from a saved output

Choose a record, step and output on Results, then click Use as data source on the right-hand Export tab. This binds a specific database record, node and output port. It does not export an image or select every curve currently plotted.

| Saved type | Reused type | Limitation |
| --- | --- | --- |
| Series | Series | Connect to tasks accepting series |
| Spectrum | Spectrum | Not automatically converted to an age-axis series |
| Grid | Grid | No automatic profile extraction or flattening |
| Table | Table | The first two columns are not automatically interpreted as X/Y |

These four node types are created by the reuse operation, rather than added as unbound tasks from the catalogue. Renaming a node cannot make its type compatible with a downstream method.

The node reads the complete saved output, not its decimated display. It cannot restore content that the original task omitted or capped. Each execution requires the original database path and record to remain valid. Moving the database, deleting the source record or changing permissions can break the reference. Switching the active results database does not migrate references. Retain the source database and backups; recreate the reference from the destination database when needed.

Reuse does not rerun the original upstream workflow. Later changes to an original input file do not update stored results. To use changed parameters, rerun and select the new record.

<a id="bound"></a>
## Generic input series

Input series is an internal binding entry that may appear in existing workflows. It is not a file picker or editable blank table. It returns the series bound by the execution context without sorting, unit conversion or reloading the original file.

It is hidden from the ordinary task catalogue. For missing-binding or type errors, inspect the workflow source or recreate it through file import or result reuse. Adding a node with the same name does not recover missing data.

<a id="odp"></a>
## Five ODP example records

Bundled records support learning and reproducible examples without a network download. X defaults to published age and can be changed to composite depth. This selects the corresponding table column; it does not calculate a new age model or resample.

| Entries | Data and chronology |
| --- | --- |
| ODP 846 oxygen and carbon isotopes | PANGAEA.60254, Mix et al. (1995); the archive itself contains author-interpolated values, not unprocessed original measurements |
| ODP 1143 oxygen isotopes | PANGAEA.132801, Tian et al. (2002); published astronomical tuning and values on the C. wuellerstorfi scale, with no additional species correction by Aster |
| ODP 926B carbon isotopes and lightness L* | PANGAEA.743431, Pälike et al. (2006); uses the optional age column readjusted to La2004, not the older La1993 column; Aster applies no extra epoch conversion |

Different proxies at one site may retain different rows because of missing selected X/Y values. Omitted pairs are counted in provenance. Do not assume row-by-row pairing or regular spacing; inspect actual coordinates before FFT, MTM or cross-spectrum analysis.

Published ages can already include astronomical tuning, so agreement with orbital periods is not independent evidence of orbital forcing. Results provenance retains fields, citations, units, missing-data handling and sampling notes. Source tables, catalogue metadata and CC BY 3.0 licensing accompany the project. Cite the data and corresponding papers when using them.

Archives: [ODP 846](https://doi.org/10.1594/PANGAEA.60254), [ODP 1143](https://doi.org/10.1594/PANGAEA.132801), [ODP 926B](https://doi.org/10.1594/PANGAEA.743431). For orbital data see the [astronomical-source guide](astronomy_usage_en.md).

<a id="sine"></a>
## Sine signal

Use this to inspect periods, detrending and spectrum operations. The formula is:

`X[i] = i × spacing`; `Y[i] = amplitude × sin(2πX[i]/period) + slope × X[i]`.

The first X is zero and the last is (count−1)×spacing. Period and spacing use the same arbitrary X unit; slope is Y change per X unit. There is no random noise. The default slope is 0.15, so set it to zero for a pure sinusoid.

For example, 512 samples, spacing 0.25, period 4, amplitude 2 and slope 0 give theoretical positive frequency 0.25. Estimated peak width depends on the method, window and record length; methods need not produce a single infinitely narrow peak. Use enough samples per cycle. Valid parameters do not guarantee adequate sampling.

<a id="synthetic"></a>
## Synthetic orbital proxy

This source combines five precession terms, five eccentricity difference-frequency terms, variable sedimentation rate and AR1 noise for method comparisons. It is not an actual orbital solution, measured ODP proxy or independent chronology.

The default is an irregular ODP 1260 depth template. Only depth locations are borrowed, not the site's proxy values. Start depth, thickness and sample count apply only to Uniform depth. That grid includes both ends with spacing thickness/(count−1). Hidden uniform-grid parameters do not modify the template.

Minimum rate must be strictly less than maximum rate, in cm/kyr (the same duration scale as cm/ka). Smooth inverse-rate variation is generated and integrated on the actual depth grid. True relative age starts at 0 ka at the first depth, with no absolute epoch. Changing the grid or rate range changes duration and the depth-domain signal.

SNR is clean-signal variance divided by noise variance, default 2, not an amplitude ratio or decibels. AR1 rho, default 0.5, refers to adjacent samples, not a fixed time/depth distance on an irregular grid. The same version, seed, realization index and settings support reproducibility. Change realization index to draw another record. The same integer seed does not imply the same sample as Python/NumPy or other software.

| Output | Meaning |
| --- | --- |
| `series` | Noisy proxy versus depth, standardized after combination |
| `clean_signal` | Clean signal with population variance 1, versus depth |
| `scaled_noise` | SNR-scaled AR1 noise versus depth |
| `age_model` | Depth versus true relative age in ka |
| `sedimentation_rate` | True rate in cm/kyr versus depth |
| `time_series` | Same noisy Y as `series`, versus true relative age |
| `harmonics` | Actual frequencies, periods and sine/cosine coefficients before clean-signal standardization |

Do not verify the outputs with series = clean_signal + scaled_noise: the sum is standardized again. The report retains the pre-standardization mean and standard deviation. Reconstruction is (clean_signal + scaled_noise − raw_mean) / raw_sd.

Regular depth does not guarantee regular age, and the irregular template is not directly suitable for FFT. Align axes, sampling and preprocessing when comparing algorithms, and retain true ages and periods as references. Behaviour under parameter changes describes this synthetic model; it does not establish that a real record follows it.

See the source [NOTICE](../../resources/synthetic/NOTICE.md) for model-port attribution and licensing; the template comes from PANGAEA.817206. Running Aster does not require Python.
