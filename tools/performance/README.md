# Joint-workflow performance checks

Build `benchmark.pro` with the same Qt, compiler, configuration and numerical
dependencies as the complete Aster build. Set `ASTER_BUILD_ROOT` to that build's
absolute directory, which contains `lib/`. Use a separate benchmark build directory.

The executable is `aster_joint_benchmark` (`.exe` on Windows). It prints one JSON
record and returns a nonzero exit code for failed calculations or validation.
Run each measurement in a fresh process, after compilation and other tests have
finished. Report at least three samples and the hardware/environment; elapsed
times are observations, not performance guarantees.

| Arguments | Scope |
| --- | --- |
| `numeric 1000000` | Two million-point inputs, Welch estimate |
| `cancel 1000000` | Cooperative cancellation requested after 50 ms |
| `plot 1000000` | 262144-point FFT, plotting, metric changes, zoom, CSV and PNG; repeated metric cycles |
| `dialog 1000` / `dialog 9999` | Pair editor opening/paint, member chooser, draft change/cancel and selected-pair preview |
| `linked 1000` / `linked 9999` | Shared cross-root reference, planning, execution and byte-identical outputs with 1/4/8 workers |
| `batch 16` | Larger same-source inputs, SQLite saving/reloading and 1/4-worker output comparison |
| `gui 512` | Actual main-window execution, cross-root inputs, result saves and 10 ms event-loop heartbeat |

`peak_rss_kib` is the process high-water resident memory on Linux/macOS; Windows
requires an external process monitor and reports `-1` for this field. The high-water
mark does not measure live allocations or prove absence of leaks. Event-loop
gaps include computation scheduling, result refresh and plotting; offscreen runs
do not measure compositor or remote-desktop latency.

The dialog measurement uses the current nonblocking window lifetime. Construction
and first-paint times are both measured from opening; member selection is timed
independently. Cancellation is verified as
cancelled, and numeric/export failures do not produce a successful benchmark.
Fixtures and databases are temporary; application settings are not saved.
