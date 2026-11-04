# AR1 global test: focused production validation

This optional tool calls the production statistic and reference cache. It does not replace the ordinary automated tests or add a new noise model.

Build the root project with `CONFIG+=release CONFIG+=no_gui CONFIG+=calibration` and the normal OpenBLAS/LAPACK + GSL dependency settings. The executable is `bin/aster_ar1_global_validation` (`.exe` on Windows).

Run all five predeclared configurations with Python 3 (standard library only):

```sh
python tools/ar1_global_validation/run.py /absolute/path/to/bin/aster_ar1_global_validation /absolute/path/to/results --jobs 2
```

Alternatively run one configuration:

```sh
aster_ar1_global_validation standard128_a.json /absolute/path/to/cache /absolute/path/to/result.json
```

There are 11,000 observation evaluations: 5 configurations × (2 null cases × 500 + 6 signal cases × 200). Different reference settings reuse paired observations at the same N; they are not 11,000 independent observations. The signal cases use rho=.98, amplitude .4/.6 relative to unit noise marginal standard deviation, and periods 8/16/32 samples. Null cases use rho=.9/.995. The optional LOWESS uses fraction .35 and zero robust iterations on both observations and references.

All cases, seeds and counts were fixed before the validation. Empty parameter sets remain in denominators and produce p=1/inconclusive. A calculation failure stops the configuration; it is not replaced with a successful draw. Wilson intervals in JSON are conditional on the realised reference bank, not a confidence interval over all possible banks or real-world noise models.

Reference files can occupy hundreds of MB and take minutes to generate. They are bounded, checked and reusable, are stored only in the chosen cache directory, and must not be added to the source archive. Use Release for speed assessment. Fixed seed does not promise bit-identical results across compilers/platforms.
