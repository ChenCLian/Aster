# Aster 1.1.1 User Guide

[中文手册](USER_GUIDE.md) · [Task and parameter reference](reference/task_parameters_en.md) · [Methods (Chinese)](methods/README.md) · [FAQ (Chinese)](../FAQ.md)

<a id="overview"></a>
## Workspace

Build a task tree on the Tasks page, inspect the selected node's output preview, then run the workflow to save results. The Results page provides database browsing, figures and export. Panel separators can be dragged; side controls collapse panels. Language, theme and concurrency are configured in Settings.

<a id="inputs"></a>
## Input data

Import delimited CSV, TXT, TSV or DAT files. Select a file and verify the delimiter, header, X/Y columns and units. Apply the configuration to that file, or to selected files with the same structure. Import and processing do not overwrite source files. Hover over a file for its full path and status.

Built-in sources include sine signals, synthetic orbital proxies, ODP records and astronomical curves. Independent sources are not repeated for each imported file. Check the age/depth axis, provenance and sampling of ODP data. Coordinate units are metadata; changing a unit label does not convert the numbers. Use an age-model task to transform depth to age.

<a id="workflow"></a>
## Tasks and parameters

Add a source, then use downstream or branch controls to connect processing and analysis tasks. Tasks can also be searched, double-clicked or dragged from the catalogue. Select the appropriate upstream output when a task has several outputs. Delete selected nodes with the toolbar or Del and check the confirmation. Canvas edits support undo and redo.

Valid parameter changes update the preview. Decimal parameters in Step settings accept scientific notation such as `1e-15`, within the declared range. Confirm with Enter or Tab; changing nodes or running the workflow also commits the current edit. A numeric editor accepts wheel changes only after deliberate click or Tab focus. A closed choice list ignores wheel selection. FFT, Welch and MTM require suitable regularly spaced input; inspect sampling and resample explicitly when necessary, or consider Lomb–Scargle for irregular sampling. Compare detrended and original branches when investigating long trends.

<a id="preview"></a>
## Original data and node output preview

The upper preview shows the source output used by the selected branch. For a source with several outputs, it follows the branch connection: MTM connected to PT-ZB18a obliquity shows obliquity above, rather than the source's default precession. Selecting the source itself and changing its output selector updates both previews without changing existing downstream connections.

The lower preview shows the selected node's output. Its selector can switch between curves, spectra, tables and grids. Select a file-reading node to preview imported data, a processing node to inspect transformed data, or an analysis node to inspect its result. The upper preview continues to show source data.

Preview runs in the background up to the selected node and does not create database records. Displayed data may be decimated; calculations use complete input. Recalculate requests a new preview and Fit restores the plotted range. Returning from a full run allows further input and node selection. A downstream failure preserves successfully generated source data and shows diagnostics below. For an empty preview, check the selected output, input file, upstream connection and parameter diagnostics.

<a id="run"></a>
## Run and inspect results

Run workflow saves complete outputs with parameters, method versions and diagnostic information. Editing parameters does not modify existing records: run again to create new ones. On the Results page, choose a record, execution step and output, then plot or export it. Diagnose the first failed step before downstream failures.

The right sidebar provides Plot, Export and Sources. Plot edits appearance; Export saves files; Sources shows parameters and provenance. The editing target identifies the active subplot. If no subplot is selected, its parameter form is hidden.

Concurrency limits simultaneously processed inputs, with a default of at most eight and no more than the available logical processors. Saved preferences are retained. Reduce concurrency for large grids or memory-intensive analyses.

<a id="plots"></a>
## Plotting and export

Hover over an axis, its ticks or title for the interaction hint. Scroll to zoom that axis and drag to pan it. Interactions inside the plot affect both axes. Fit restores the data range.

The fixed X/Y selector at the top of the Axes page chooses the editing target. Each axis keeps its own range, title, scale, direction, ticks and grid. Log display requires positive values; ranges are entered in original coordinates. It does not transform stored data.

MTM's frequency upper limit uses inverse input-axis units. It crops the output spectrum, threshold curves and peak table, affecting saved results and downstream data. Estimation, background fitting and peak detection still operate on the full spectrum. Use axis zoom to change only the view. The editor displays numeric `0` with a separate automatic-limit hint; zero selects the automatic limit for new nodes; if an older node retains a legacy ratio, explicitly enter zero and confirm with Enter or Tab to restore the full band.

Click samples, peaks or grid cells to inspect coordinates. Positive frequencies can show reciprocal periods. Decimated preview readings refer to displayed samples. Esc, clicking blank space or changing the view dismisses the readout. CSV exports complete selected outputs; PNG/PDF exports figures and styles subject to display budgets. See [plotting details (Chinese)](ui/plotting.md) and [statistical limits (Chinese)](methods/statistical_limits.md).

<a id="database"></a>
## Database and selection

SQLite stores results. Changing the database path does not copy or delete records in the previous database. Use the Database menu for backup and merging compatible Aster databases. Imported records receive new IDs; incompatible schemas are rejected.

Browsing a record and checking it for a bulk operation are separate. Filtering and pagination retain checks. Select all checks every matching record, including unloaded pages. Uncheck removes checks from current filter matches. Clear all removes all checks, including records outside the filter; it does not delete data. Delete checked requires confirmation. Back up before bulk deletion and inspect the count outside the current filter.

<a id="shortcuts"></a>
## Keyboard shortcuts

Buttons and panel grips include shortcuts in their tooltips. The table uses Windows/Linux notation; the interface displays native key names.

| Key | Action |
| --- | --- |
| F1 | Open the user guide, including from Settings |
| Ctrl+, | Open Settings |
| Ctrl+1 / Ctrl+2 | Switch to Tasks / Results |
| Ctrl+Shift+1 | Toggle the preview panel |
| Ctrl+Shift+2 | Toggle step settings |
| Ctrl+Shift+3 | Toggle the task catalogue |
| Esc | Close the current dialog or dismiss a plot readout, depending on the active control |

Panel shortcuts apply only on Tasks. Main-window navigation does not activate through modal dialogs.

<a id="help"></a>
## Help

Press F1 in the main window or choose Settings → User guide. Hover over a parameter's question mark for a short explanation, or click it to open the matching manual section. Method help opens the method's section. The user guide and parameter reference follow the interface language. Chinese entries without a translation use English. Detailed scientific documents are currently in Chinese.

The built-in viewer supports Back, Forward and Find on page. Enter searches forward with wraparound; Ctrl+F focuses the search field. The manual works offline. View on GitHub opens the matching online page. Missing local pages or sections fall back to GitHub; online documentation reflects the repository's current version.

Settings → Contact us opens the authors and contact page. Settings → License opens the offline copyright notice, license terms and source information.

<a id="troubleshooting"></a>
## Troubleshooting and updates

Missing Chinese glyphs usually require a CJK font; Linux/WSL instructions are in the [FAQ](../FAQ.md#linux-fonts). Linux startup also requires a working graphical environment and platform libraries. Evaluate input size, concurrency and graphics separately when investigating slow interaction; WSL performance does not establish native desktop performance.

COCO requires a depth axis. The metre-valued composite-depth labels `mcd` and `rmcd` are accepted with depth unit `m`, preserving the source depth reference. Resampling does not convert age to depth. COCO Monte Carlo allows 19–20000 simulations, subject to a separate fixed budget of 10 billion estimated work units per task. All scan-adjusted p values being one can be a valid result. See the [COCO FAQ (Chinese)](../FAQ.md#coco-units) for interpretation and budget diagnostics.

Settings can check releases and open the download page. This version uses complete-package manual updates without background installation. Keep the database and settings when replacing a program package. Reports should include version, platform, reproduction steps and errors; remove personal paths, credentials and confidential data.
