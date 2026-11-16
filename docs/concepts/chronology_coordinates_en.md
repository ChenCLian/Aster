# Shared conventions for depth, age and rate

This page is shared by age models, cycle counting, COCO and astronomical data. See the [age-model guide](../methods/chronology_usage_en.md), [COCO guide](../methods/coco_usage_en.md) and [astronomical-data guide](../methods/astronomy_usage_en.md) for task-specific instructions.

<a id="coordinates"></a>
## Establish what X represents

Depth, age and frequency are different coordinates. Renaming an axis or its unit does not convert depth to age. An age model requires a depth–age relationship; COCO compares spectra under candidate sedimentation rates without converting the entire record.

These depth tasks require strictly increasing X. Investigate duplicates, reversed order and nonfinite values before deciding how to clean the record. The program does not repair stratigraphy. Controls with increasing depth but decreasing age also violate the current age-model convention.

<a id="units"></a>
## Units and depth datums

Depth choices are m, dm, cm, mm, ft and km; age choices are yr, ka and Ma. ka denotes thousands of years and Ma millions. Check input units, control-table units and output units explicitly.

A nonempty input unit must agree with the selected parameter. Unlabelled input is interpreted using the explicit parameter, with a warning. mcd and rmcd are treated on a metre length scale; composite and revised depth datums are not converted. Two records labelled in metres can still require depth alignment.

COCO candidate rates use cm/ka, target periods ka and frequencies 1/ka. For example, a depth interval of 0.02 m at 2 cm/ka represents 1 ka. This illustrates conversion, not a recommended rate.

<a id="epoch"></a>
## An age-reference label does not transform values

The age-model reference options—unspecified, BP (1950), before 2000 and relative age—record meaning. Changing the selection does not shift ages or calibrate radiocarbon dates. Align the first age, control table and comparison data yourself.

Built-in astronomical curves use ka before J2000, positive into the past. Under the year-reference convention used here, the same instant has age(J2000) = age(BP1950) + 0.05 ka. This offset does not replace radiocarbon calibration, calendar conversion or a geological age model.

The first age belongs to the actual first depth or cycle marker, not necessarily depth zero. Relative age zero need not represent the present.

<a id="coverage"></a>
## Coverage, boundaries and uncertainty

Ages are interpolated piecewise linearly between controls. Apply age model rejects input outside control coverage by default; it does not crop automatically. Crop explicitly or enable endpoint linear extrapolation when justified. Extrapolation extends the first or last control interval and adds no age evidence.

The current model does not estimate age confidence intervals, propagate control errors, infer missing cycles or model hiatuses and erosion automatically. Positive rates and increasing controls cannot represent every stratigraphic process.

<a id="sampling"></a>
## Check sampling after changing the age axis

Applying an age model preserves Y and sample count and replaces X. It does not create a regular time grid. Regular depth sampling generally becomes irregular in age under a variable-rate model.

If the next method requires regular sampling, inspect the new axis and decide whether resampling is justified. Interpolation adds no original information. See [sampling and resampling](sampling_en.md). Also check sampling density and control coverage when comparing records over the same age interval.

<a id="files"></a>
## External model files and saved results

The age-control table and optional PT orbital table are read as file parameters. Execution records information about the content actually used. Later changes to an external file do not rewrite saved results.

Rerunning requires the current file to be readable. Recalculate explicitly after editing its content; an existing preview may still represent the old file. Keep files stable during a batch. A content snapshot for one node execution is not a frozen file shared by the entire batch.
