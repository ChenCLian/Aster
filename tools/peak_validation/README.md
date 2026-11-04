# Peak reference comparison

Optional development check; Python/SciPy is not a runtime dependency of Aster.
The application uses the C++ code in `src/math/spectrum/spectrum_peaks.cpp`.

From the project root, on Linux with GCC/Clang:

```sh
mkdir -p build-validation
c++ -std=c++17 -O2 -shared -fPIC -Isrc tools/peak_validation/reference_bridge.cpp src/math/spectrum/spectrum_peaks.cpp -o build-validation/aster_peak_reference.so
python tools/peak_validation/compare_scipy.py build-validation/aster_peak_reference.so --output build-validation/peak_reference.json
```

On Windows, use the x64 MSVC developer command prompt from the project root:

```bat
cl /std:c++17 /O2 /EHsc /LD /Isrc tools/peak_validation/reference_bridge.cpp src/math/spectrum/spectrum_peaks.cpp /link /OUT:aster_peak_reference.dll
python tools/peak_validation/compare_scipy.py aster_peak_reference.dll --output peak_reference.json
```

Install NumPy and SciPy in a separate Python environment if needed. The recorded independent comparison used NumPy 2.3.5 / SciPy 1.17.0 on Linux; that archived run does not establish a Windows comparison result.

Seed 3403 generates 900 cases covering plateaus, random samples, decaying tails, wide amplitude scales, absolute/relative prominence and local/full search windows. It checks peak indices, prominence and half-prominence width against `scipy.signal.find_peaks`. Minimum distance is 1 in this comparison: Aster deliberately resolves competing peaks by prominence, then height, then leftmost index, whereas SciPy uses height priority. That policy is covered by the C++ regression tests. This is an amplitude/local-maximum check, not a statistical significance calibration.

Reference: https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.find_peaks.html
