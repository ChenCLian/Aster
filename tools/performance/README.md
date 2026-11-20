# Performance checks

Build the main Aster project in Release mode first, including its static libraries.
Use the same Qt kit, compiler and scientific dependencies for this separate target:

```sh
mkdir -p build/performance-recent
cd build/performance-recent
qmake /path/to/Aster/tools/performance/recent_features.pro CONFIG+=release ASTER_BUILD_ROOT=/absolute/path/to/Aster-build
make -j4
```

With MSVC, use the kit's qmake and matching make tool. These commands do not install dependencies. Run each case in a fresh process and capture stdout as JSON; diagnostics go to stderr. A nonzero exit means a correctness assertion or setup failed.

```sh
./aster_recent_features_benchmark import 1
./aster_recent_features_benchmark import 100
./aster_recent_features_benchmark import 1000
./aster_recent_features_benchmark import 10000
./aster_recent_features_benchmark remove 10000
./aster_recent_features_benchmark numeric
./aster_recent_features_benchmark cancel
./aster_recent_features_benchmark plot
```

Set `QT_QPA_PLATFORM=offscreen` for headless Linux. Remove it when checking a native desktop. QtTest sends widget events; it does not reproduce an operating system's complete keyboard, mouse, input-method, scaling or window-manager behavior.

| Mode | Workload and validation |
| --- | --- |
| import | Create 1, 100, 1000 or 10000 tiny files with four data rows each. Open the import settings, select all inputs and type an X unit. Check the saved settings for every input. File creation is outside the timing interval. |
| remove | Select all, click Remove and verify the current workflow has no remaining source files. |
| numeric | 1001/10001/100001 samples; fixed window 200, hop 64, FFT 800, linear detrending and rectangular taper; also 20% span / 4× padding with either legacy 99.2% overlap or a target of 500 windows. One warmup, ten measured executions per case. The target mode must succeed at 1001 and 10001 samples; the legacy overlap mode at 1001. Larger preset cases must return `EVOLUTION_BUDGET`; rejection time is not computation time. |
| cancel | 400001 samples, window 256, hop 64, FFT 1024, maximum frequency 0.02. Request cancellation from another thread after approximately 5 ms and require a cancelled result. |
| plot | 256×1024 grid, 900×650 widget. Ten warmup and 100 measured amplitude/interpolation changes with event processing. Verify the original encoded data hash is unchanged. |

Times are wall-clock milliseconds. Import timer lateness includes initial import work and is not native input latency. Numeric and plot timing summarize repeated executions; file-operation and cancellation results are single trials. Repeat fresh processes for statistical comparisons and avoid concurrent workloads. `rss_kib` uses Linux `/proc`; `peak_rss_kib` uses supported Unix process accounting. Unsupported metrics return -1. Stable RSS over this workload is not proof of no leaks.

Each benchmark prints its measured environment and results as JSON. The UI suite also checks selection capture, concurrent edits, same-path reimport, undo/redo, and narrow translated layouts.

## Paired workflows

Build `tools/performance/benchmark.pro` in a separate directory with the same `ASTER_BUILD_ROOT`. Run `aster_joint_benchmark linked 4` to exercise dependencies across source trees with four datasets. The `dialog`, `batch`, `gui`, `numeric`, `plot` and `cancel` modes exercise pairing, scheduling, desktop interaction or the Welch cross-spectrum calculation.
