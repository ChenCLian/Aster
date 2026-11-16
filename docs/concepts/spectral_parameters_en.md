# Windows, FFT and spectral displays

[Methods](../methods/README.md) · [Power-spectrum task guide](../methods/power_spectra_en.md) · [中文](spectral_parameters.md)

This page explains shared concepts. Defaults, allowed ranges and task-specific conversions remain in each task's guide and parameter reference.

<a id="windows"></a>
## Three meanings of window

| Term | Purpose | Example |
| --- | --- | --- |
| Analysis window or segment | Select observations for one spectral estimate | Welch segment length; evolutionary-spectrum window length |
| Smoothing neighborhood | Use neighboring samples or frequencies to estimate a smoothed value | Moving-average sample window; spectral-background smoothing range |
| Taper or window function | Weight samples within an analysis segment | Hann, rectangular; multiple DPSS tapers in MTM |

The word window alone does not identify its units or function. A moving-average sample count and an MTM background-smoothing frequency fraction are not interchangeable.

Relate analysis length to the periods of interest. A window containing only one or very few cycles has limited ability to separate nearby frequencies. Longer windows generally improve separation but mix changes over a longer interval; shorter windows localize changes with fewer observations per spectral estimate. No fixed percentage is universally best.

For L equally spaced samples at spacing dx, the endpoint span is `(L−1)×dx`, while the duration used to define the FFT frequency scale is `L×dx`. They differ by one sampling interval. Changing window length changes the observations used; increasing FFT length alone does not.

<a id="overlap"></a>
## Step, overlap and window count

Step S is the separation of successive segment starts in samples, or `S×dx` in X units. With L samples and O overlapping samples, `S=L−O` and overlap percentage is `100×O/L`. Welch takes overlap in samples; evolutionary overlap mode takes a percentage. Do not copy the same numerical value between them.

Steps are integers, so the requested percentage may not be attainable exactly. With L=100, 99.2% suggests a step of 0.8 samples; rounding and requiring at least one sample gives 99% actual overlap.

For N input samples and complete windows only, count is `floor((N−L)/S)+1`, with `N≥L`. Smaller steps increase position density and shared observations. They add neither independent observations nor the frequency resolution lost through a short window.

See [evolutionary settings](../methods/power_spectra_en.md#evolution-settings) for target counts, tail handling and step constraints. Use the calculation report for actual overlap.

<a id="taper-trend"></a>
## Detrending and tapers

Demeaning subtracts the segment mean. Linear detrending subtracts a fitted line, including intercept and slope. Both change the data used in spectral estimation, especially its low-frequency content. Inspect the original record and detrending settings before interpreting a weakened low-frequency component as an absence of long cycles.

A Hann taper downweights segment edges and generally reduces leakage from finite truncation, while changing peak width. A rectangular taper uses equal weights and is more sensitive to discontinuous edges. Taper selection changes the estimate; heatmap pixel interpolation does not.

MTM's DPSS tapers are different weightings of the same analysis segment, not different moving positions. Time-bandwidth NW controls a spectral smoothing/resolution tradeoff; target window count controls the density of analyzed positions.

<a id="fft-padding"></a>
## FFT length and zero padding

The FFT must accommodate the actual analysis segment. For NFFT samples, frequency-grid spacing is `1/(NFFT×dx)`. Increasing NFFT samples the estimate more densely in frequency; it adds no observation duration and does not itself improve separation of nearby cycles.

For 100 samples at 0.2 ka spacing, NFFT=100 gives 0.05 /ka frequency spacing; NFFT=400 gives 0.0125 /ka. The same 100 observations are used. Window duration, taper, MTM bandwidth and signal-to-noise ratio still affect peak width and resolvability.

FFT length 0 means the full input length for the full-record FFT, segment length for Welch and analysis-window length for evolutionary spectra. It does not mean universally padding to the next power of two.

Padding an extracted segment is different from adding samples to both ends of a record before sliding windows. The four power-spectrum tasks covered here use complete segments and do not add edge windows by padding the record.

<a id="frequency"></a>
## Frequency, period and upper limit

Frequency has reciprocal X units. For X in ka, 0.01 /ka corresponds to a period of 100 ka; for X in m, it corresponds to a spatial wavelength of 100 m. Converting depth-domain frequency to time requires an explicit depth–time relation. Zero frequency has no finite period.

The Nyquist frequency is `1/(2×dx)`. Zero padding does not change it. The highest finite FFT grid point need not equal Nyquist exactly, especially for odd NFFT.

The evolutionary upper-frequency limit crops the calculated map and mean-spectrum outputs; it is not merely an axis zoom. A smaller limit reduces output-grid size but does not shorten each FFT or redefine the normalization maximum within the visible band.

<a id="power-units"></a>
## PSD, power and amplitude

FFT, Welch and evolutionary spectra without per-window normalization output one-sided power spectral density (PSD). For X in ka and Y in u, PSD has units `u²·ka`, with frequency in `/ka`. Missing input units are not guessed.

PSD peak height is not the original signal amplitude: grid spacing, taper and peak width affect it. Integrating PSD over frequency yields a corresponding squared quantity; detrending and tapering affect its relation to the original record's variance. Align input units, sampling reference and estimator settings before comparing records.

<a id="normalization"></a>
## Per-window normalization changes stored power

With normalization disabled, evolutionary outputs retain PSD. Enabling it divides each window by its full one-sided spectral maximum, producing dimensionless relative power; zero-power windows remain zero. The denominator is determined before frequency cropping, so the maximum within a cropped visible band may be below 1.

This assists comparisons of shape and peak position but removes absolute strength differences between windows. Strong, weak and noise-dominated windows may all show equally bright relative peaks. Brightness is not evidence of significance. Map values, CSV and the mean window spectrum change with this calculation option; rerun to obtain the other output.

The mean window spectrum is an arithmetic average of window outputs. With normalization enabled it averages relative power, rather than normalizing the mean PSD afterward. It is also not the spectrum estimated directly from the full record.

<a id="display"></a>
## Display settings change the image, not calculated values

| Setting | Effect | Stored power and numerical export |
| --- | --- | --- |
| Square-root power display (formerly amplitude) | Square root of nonnegative power: relative amplitude after normalization, otherwise square root of PSD | Remain power, as do probes and the mean spectrum |
| Logarithmic color mapping | Map positive values to colors logarithmically; nonpositive cells are blank | Unchanged; not a log transformation of input Y |
| Smooth display | Interpolate rendered pixels to soften cell boundaries | Unchanged; no additional calculated windows or frequencies |
| Color range and palette | Change the color assigned to a value | Unchanged; align displayed quantity, mapping and range across plots |

Square root of PSD is still not signal amplitude; its units are the square root of PSD units. Image export records the visual appearance, while CSV records calculated values, so its numbers need not equal the displayed color-scale numbers.

A more continuous image may result from denser window positions, denser frequency sampling, per-window normalization, square-root/log mapping or pixel interpolation. Compare numerical values, coordinates and units first, then align display settings before comparing images.

Across software, also align actual integer lengths and steps, detrending, tapers, edge padding, frequency range and normalization reference. Matching a requested 99.2% overlap or a palette alone does not establish equivalent settings.
