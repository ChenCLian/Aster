# Cross-spectrum reference fixtures

The application does not require Python, NumPy or SciPy. These files support independent numerical regression checks.

- `generate_reference.py` / `scipy_reference.json`: synthetic cases (existing alpha.2 evidence).
- `generate_real_reference.py` / `real_reference.json`: eight configurations using the attributed ODP tables in `resources/examples/`. Source SHA256 values, citations, DOI, licenses and chronology descriptions are included in the fixture. Reproduction used NumPy 2.3.5 and SciPy 1.17.0.

Run a generator from a Python environment containing those validation dependencies. It writes the corresponding JSON beside itself. The real-data generator verifies source hashes before reading the tables.

## Data attribution

The ODP inputs and their interpolated derivatives in `real_reference.json` retain [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) attribution; Aster's code license does not replace the source-data terms. These fixtures average duplicate coordinates, interpolate and calculate spectra as described below.

- Mix, A. C.; Le, J.; Shackleton, N. J. (1995), interpolated stable carbon and oxygen isotope ratios, ODP Site 138-846: [PANGAEA.60254](https://doi.org/10.1594/PANGAEA.60254).
- Pälike, H.; Frazier, J.; Zachos, J. C. (2006), age models, stable isotope record and physical properties, ODP Hole 154-926B: [PANGAEA.743431](https://doi.org/10.1594/PANGAEA.743431).

## Real-data preparation

ODP 846 δ18O/δ13C use the published age column. Sort coordinates, average duplicates, then linearly interpolate onto a common 1 ka grid. The C++ test independently executes the production preprocessing task and compares its X/Y values to the NumPy grid and interpolation.

ODP 926 δ13C/lightness have different valid rows and endpoints. Prepare a common 5 ka grid over their shared interval explicitly in the fixture. This is external preparation, not an automatic alignment feature of Aster. A separate test verifies that independently resampling each native record at 5 ka still produces incompatible lengths and is rejected by the joint task.

The four parameter sets per pair cover periodic Hann and rectangular windows, no/constant/linear detrending, odd and even FFT sizes, zero-padding, and different overlaps. Tests compare both complex components, density scaling, autospectra, coherence and circular phase differences. Very weak complex values are checked as components; their numerically unstable phase is not used as an accuracy criterion.

## Analytic zero-frequency exception

SciPy's raw reference rows are retained without rewriting. Rectangular-window detrending removes the segment mean (linear detrending includes an intercept), so DC should be exactly zero in real arithmetic. Tiny floating-point residuals can produce arbitrary coherence and phase at this bin. Aster method version 2 sets only this structurally zero DC bin to zero and marks coherence/phase undefined. Tests compare its near-zero density values with spectral tolerances and verify undefined masks instead of comparing ratios of SciPy's roundoff. Separate tests cover both detrending modes and padded/unpadded FFTs; Hann and undetrended rectangular DC remain unmodified.

This validation does not establish statistical significance, causal lead/lag, or astronomical forcing. Published tuned age models are not independent astronomical evidence. See the versioned validation records under `docs/validation_results/` for tolerances, environments and limits.
