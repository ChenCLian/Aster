# Choosing astronomical curves and insolation outputs

These tasks are standalone data sources: no imported file is required. Add a source, choose its output and connect further processing. Period envelopes are covered separately under [target periods](chronology_usage_en.md#periods).

<a id="grid"></a>
## Age range and spacing

Ages are ka before J2000, positive into the past. See [age references](../concepts/chronology_coordinates_en.md#epoch) for the difference from BP1950. The default range is 0–2000 ka with 1 ka spacing.

The grid is start + integer × spacing, not exceeding the end. An off-grid end is not appended with a shorter final interval. Smaller spacing densifies interpolation without improving source resolution; excessively coarse spacing can miss short cycles. Check actual endpoint and sample count.

<a id="orbital"></a>
## Choose an orbital dataset

| Source | Quantities | Requestable age range |
| --- | --- | --- |
| La2004 orbital curves | Eccentricity, obliquity, climatic precession | 0–50000 ka |
| Orbital eccentricity: La2010a–d | Eccentricity | 0–249999 ka |
| Orbital eccentricity: ZB18a / ZB20a–d | Eccentricity | 0–300000 ka |
| PT-ZB18a obliquity and precession | Obliquity, climatic precession and precession angle in its stated frame | 0–100000 ka |

Eccentricity and climatic precession are dimensionless; obliquity is output in degrees and the PT angle in radians. Orbital inclination is not obliquity. The PT angle cannot directly replace La2004 perihelion longitude. Eccentricity or climatic precession alone does not provide a complete insolation input; insolation here uses La2004.

Data coverage does not imply equally reliable phase throughout. Assess La2010 phases older than about 50 Ma and ZB phases older than about 66 Ma using the original papers; these are not exact error boundaries. PT also depends on dynamical ellipticity and tidal dissipation.

Source spacing is 1 ka for La2004/La2010, 1.6 ka for ZB and irregular for PT. See [orbital tables](orbital_solutions.md) and the [La2004 definition](astronomy.md) for scalar interpolation, angle wrap handling, sources and hashes.

<a id="pt"></a>
## Optional PT table

Leave the file parameter empty to use the built-in Ed=1, Td=1 table. For other settings, select the corresponding official PT file and preserve the filename encoding Ed and Td. It must have the full prescribed coverage and pass format, ordering and physical-range checks. A bad file fails instead of falling back silently.

See [external PT tables](orbital_solutions.md) for the download tool and supported parameter grid. Local validation can detect content or format problems but cannot authenticate provenance. Results record the actual filename, parameters and hash. Recalculate after editing the file.

<a id="etp"></a>
## ETP composite

ETP z-standardizes eccentricity, obliquity and climatic precession separately on the requested output grid and combines them with E, T and P weights. Defaults are 1, 1 and −1. The combined curve is not standardized again.

Standard deviation uses the N−1 denominator. Changing the range or sampling interval changes means and standard deviations, so values in an overlapping interval can also change. For comparisons requiring a common scale, define a common reference interval separately; z scores from separate intervals are not automatically comparable.

Weights are linear coefficients on standardized components, not COCO relative powers. Zero-weight components need no standardization. A nonzero-weight component with zero variance fails; all-zero weights may produce a zero curve. See the [ETP definition](astronomy.md).

<a id="daily"></a>
## Daily mean insolation

The output is top-of-atmosphere daily mean irradiance at a specified age, latitude and seasonal position, in W/m². It is not surface energy, sunshine duration or seasonal accumulated energy.

Defaults are 65°N, solar longitude 90° and solar constant 1365 W/m². Latitude is positive north and negative south. Longitudes 0°, 90°, 180° and 270° denote the equinoxes and Northern Hemisphere summer/winter solstices; 90° is not the Southern Hemisphere summer solstice.

Choose solar longitude or a fixed month/day. Month/day mode uses the orbital position at date noon; it is not a year-specific historical Gregorian conversion. Shared seasonal conventions follow below.

<a id="season"></a>
## Seasonal definitions and integrated outputs

Month/day mode uses fixed non-leap month lengths, with February limited to 28 days and the equinox fixed at noon on March 21. The 365 date cells map to a year of 365.24219876 days of 24 hours. Date-cell counts and output duration are therefore different quantities.

Integration runs from the start date at midnight to the end date at midnight, excluding the end day. June 1 to September 1 includes June–August. An earlier end wraps across year-end; equal start/end means a full year. Solar longitude advances in the positive direction; 270° to 90° crosses zero, and equal endpoints also mean a full year.

| Seasonal output | Unit and meaning |
| --- | --- |
| `season_mean` | W/m², time-weighted seasonal mean irradiance |
| `season_energy` | MJ/m², accumulated seasonal energy |
| `season_duration` | Days of 24 hours used in integration |

Energy = mean irradiance × duration in days × 86400 / 1000000. The same longitude interval can have different durations at different ages. An arithmetic mean of equally spaced longitude samples is not a time mean. Mean irradiance and accumulated energy answer different questions; align outputs and interval conventions before comparing.

Seasonal integration allows up to 20000 age samples. See the [seasonal definition](seasonal_insolation.md) for formulas, date boundaries and reference-implementation differences.

<a id="map"></a>
## Latitude–age insolation map

The map repeats daily mean insolation across latitudes. X is age, Y latitude and colour W/m². It shows daily means at the specified seasonal position, not seasonal integration.

Default latitudes are −90° to 90° in 5° steps, positive north and negative south. At least two increasing latitudes are required. Date/longitude, solar constant and age grid follow the daily task. Limits are 361 latitudes, 20000 ages and two million cells, all applying together. If exceeded, increase spacing or narrow the range. Changing colour limits does not change computational size.
