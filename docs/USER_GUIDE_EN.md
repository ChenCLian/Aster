# Aster 1.2.0-beta.7 User Guide

[中文手册](USER_GUIDE.md) · [Task and parameter reference](reference/task_parameters_en.md) · [Methods (Chinese)](methods/README.md) · [FAQ (Chinese)](../FAQ.md)

## Download and start

**No installation is required. Extract the complete package and launch Aster.** On Windows, open `Aster.exe`; on macOS, open `Aster.app`; on Linux, run `./AppRun`. Download the application package for your operating system and keep all its files together. Source archives are intended for development and compilation.

If Windows reports missing `VCRUNTIME` or `MSVCP` files, install the [Microsoft Visual C++ x64 runtime](https://aka.ms/vc14/vc_redist.x64.exe). Linux requires compatible system libraries, X11/XWayland and fonts. macOS packages require macOS 15+ and the correct processor architecture; signing information is provided with the release. See the [startup FAQ (Chinese)](../FAQ.md#portable).

<a id="overview"></a>

## Workspace

Build a task tree on the Tasks page, inspect the selected node's output preview, then run the workflow to save results. The Results page provides database browsing, figures and export. Panel separators can be dragged; side controls collapse panels. Language, theme and concurrency are configured in Settings.

<a id="inputs"></a>

## Input data

Import delimited CSV, TXT, TSV or DAT files. Select a file and verify the delimiter, header, X/Y columns and units. Apply the configuration to that file, or to selected files with the same structure. Import and processing do not overwrite source files. Hover over a file for its full path and status.

Built-in sources include sine signals, synthetic orbital proxies, ODP records and astronomical curves. Independent sources are not repeated for each imported file. Check the age/depth axis, provenance and sampling of ODP data. Coordinate units are metadata; changing a unit label does not convert the numbers. Use an age-model task to transform depth to age.

<a id="workflow"></a>

## Tasks and parameters

Add a source, then use downstream or branch controls to connect processing and analysis tasks. Tasks can also be searched, double-clicked or dragged from the catalogue. Select the appropriate upstream output when a task has several outputs. Delete selected nodes with the toolbar or Del and check the confirmation. Nodes, connections, parameters, imported file lists, file import options and pairs share one undo/redo history, limited to 100 effective edits. Undoing a node deletion restores its source bindings and pairs; undoing input removal restores the original dataset identity. Undo does not restore file contents on disk or remove saved results. Preserve original data separately. Workflow editing is disabled during a run.

When several compatible outputs are available, the chooser opens near the source node. Rows have a consistent height and the default output is bold. More than eight choices enable search and scrolling. Hover for the full name; click or press Enter to confirm. Escape or clicking outside cancels without adding a task. A single compatible output connects directly.

Click the small “+” on an existing connection, or drop a task there, to insert a step. To replace an ordinary node, use its context menu or drop another task onto the node. The review dialog lists the new input and the output for each downstream branch. Check their meaning: compatible data types do not imply equivalent scientific content. Ambiguous outputs require an explicit choice. The new method starts with its own defaults; adjust them in Step settings after confirmation. Replacement preserves downstream branches; insertion preserves existing nodes.

Nothing changes before confirmation. Cancel leaves the workflow untouched; one undo restores an insertion or replacement. Source nodes, joint nodes and edits directly involving joint inputs or outputs are not enabled. Insertion requires room for the whole downstream subtree in the logical grid. If no placement is available, the workflow remains unchanged and an explanation is shown.

Valid parameter changes update the preview. Decimal parameters in Step settings accept scientific notation such as `1e-15`, within the declared range. Confirm with Enter or Tab; changing nodes or running the workflow also commits the current edit. A numeric editor accepts wheel changes only after deliberate click or Tab focus. A closed choice list ignores wheel selection. FFT, Welch and MTM require suitable regularly spaced input; inspect sampling and resample explicitly when necessary, or consider Lomb–Scargle for irregular sampling. Compare detrended and original branches when investigating long trends.

For continuous wavelet analysis, choose the calculation convention by its power definition. The default is variance-normalized, scale-corrected power; the alternative is raw power (Torrence–Compo). Their scale ranges and AR(1) estimates also differ. This choice affects computation, while log axes, reversed axes and colors only affect plotting. See the [wavelet method guide (Chinese)](methods/cwt.md).

<a id="evolution-spectrum"></a>
## Evolutionary FFT and MTM

Evolutionary spectra estimate power in moving windows of a strictly increasing, regularly sampled series. Window length and step are entered in samples. Enter the maximum frequency directly in reciprocal X units, such as 1/m or 1/ka. Zero selects Nyquist automatically; larger values are clamped to Nyquist. Saved legacy ratios remain active until the maximum-frequency field is explicitly edited, including re-entering and confirming zero. The limit crops the saved map and mean spectrum; narrowing it does not improve resolution.

For sample spacing dx, N input samples, window length L and step S:

- Endpoint span is (L-1)*dx; Fourier duration is L*dx; physical step is S*dx.
- The number of complete windows is floor((N-L)/S)+1. No samples are synthesized at either end.
- FFT length 0 uses L; an explicit length must be at least L. Frequency spacing is 1/(FFT length*dx). Zero padding densifies the grid without increasing physical frequency resolution.

For example, 705 samples at spacing 0.2 with window 128, step 16 and FFT 0 produce a 37-by-65 map. Window 100, step 1, FFT 400 and maximum frequency 2 produce 606-by-161. This illustrates the conversion, not a universal parameter recommendation. Smaller steps and longer FFTs increase computation. Preview and result views may sample the grid to fit the display; CSV retains the complete calculated outputs.

Normalize each window is off by default, preserving physical PSD. When enabled, divide each window by its maximum power over the full one-sided spectrum; all-zero windows remain zero. Both the map and mean window spectrum then contain dimensionless relative power. This supports comparison of periodic structure but removes absolute power differences between windows. The normalization reference is independent of output cropping. Neither output supplies a significance test, and the mean window spectrum is not the full-record estimate.

Preview and result heatmaps retain discrete cells and linear color mapping by default. In the result plot's heatmap settings, logarithmic color mapping and smooth display remain available without altering calculated values. Parameter help explains the same conversions.


<a id="cross-spectrum"></a>

## Welch cross spectra and A/B pairs

1. Prepare the series in the current workspace and verify both X/Y columns, X names and units. A and B may come from one upstream node, different ordinary processing branches, or different source roots. Two distinct outputs from one built-in source may also form a pair.
2. Add **Welch cross-spectrum analysis** after an ordinary data path. Complete any preprocessing on each input path first. The tree connection determines the joint node’s placement; its actual calculation inputs are the configured A/B references.
3. Open **Manage pairs…** in Step settings and add a named pair. Click an A or B cell (or press Enter), select a source node, then select its dataset and output and click **Use this input**. Node numbers match the canvas tooltips. Search nodes, datasets or outputs; hover to see full names. Confirm the pair editor to save the changes. A reference may be reused. Self-pairs with the same node, dataset and port, and duplicate ordered pairs, are rejected. Outputs of different ordinary processing nodes may use the same dataset.
4. Select a **Preview pair**. Switch A/B above the preview to inspect the actual calculation inputs. Below it, select coherence, cross-spectrum magnitude, phase or another metric. The canvas shows that pair’s A/B reference lines. This selection changes only the preview and reference lines; a formal run computes every configured pair.
5. Set segment length, overlap, FFT length, window and detrending. The defaults are 256 points per segment and 128 overlap points; changing segment length does not automatically change overlap, which must remain smaller than the segment. At least two complete segments are required. For 256 input samples, 128-point segments with 64-point overlap give three segments. Incomplete trailing samples are discarded. FFT length 0 uses the segment length. Zero padding does not increase the original resolving power.
6. Run the workflow and open the pair's record. Ordinary branches and upstream results are saved per dataset; joint results are saved per pair with ordered source links, parameters and the upstream steps needed by that pair. CSV exports all columns with units. PNG/PDF exports the selected metric and plot style.

With a rectangular window, segment mean removal or linear detrending removes DC. Coherence and phase at zero frequency are therefore undefined, not failed calculations. This special rule does not apply to Hann windows or nonzero frequencies.

Resampling two records at the same spacing does not necessarily give them the same start or length. Prepare and verify a shared interval, start and spacing before pairing; the joint node does not crop or interpolate automatically. Published age models in built-in ODP records may already incorporate astronomical tuning, so cross-spectral agreement alone is not independent evidence of astronomical forcing.

Inputs must have equal lengths, strictly increasing uniform X grids, and matching X names and units. Corresponding coordinates must agree within one millionth of the sample interval. No implicit interpolation, unit conversion, chronology conversion or row-index matching is performed.

See the [cross-spectrum method guide (Chinese)](methods/cross_spectrum.md) for output columns and limits.

Magnitude-squared coherence lies between 0 and 1; no significance threshold is provided. Phase uses `conj(A) * B` along increasing numeric X. Swapping A/B reverses phase. For ages increasing into the past, do not directly apply a forward-time lead/lag interpretation. Undefined coherence/phase at zero power appears as an em dash in tables, an empty CSV field, and a gap in curves.

Unassigned series are excluded from the joint node while ordinary branches still run. Removing a referenced node or dataset marks its pairs unavailable and shows A! or B! on the canvas. Repair the input in Manage pairs or undo the deletion; reimporting the same filename does not silently replace it. Cancel discards pair edits. A workflow or source change closes any open chooser; reopen it to continue. Preview and formal execution read current source files. Recalculate after changing files, and preserve input files separately: the batch manifest is not a byte snapshot.

Use a separate test database for this beta release. Version 1.1.1 cannot read new masked cross-spectrum tables. Pairing across ordinary branches and source roots in the current workspace is supported. Direct links to a different saved run and pairing outputs from joint nodes or their descendants are not supported. Import saved results as ordinary database sources before using them as inputs.

<a id="preview"></a>

## Source data, calculation inputs and node output preview

The upper preview shows the source output used by the selected branch. For a source with several outputs, it follows the branch connection: MTM connected to PT-ZB18a obliquity shows obliquity above, rather than the source's default precession. Selecting the source restores its last browsed output; its output selector updates both previews without changing downstream connections. A source may feed several branches through different ports, so returning to it does not adopt one branch’s input selection.

The lower preview shows the selected node's output. Its selector can switch between curves, spectra, tables and grids. Select a file-reading node to preview imported data, a processing node to inspect transformed data, or an analysis node to inspect its result. On ordinary single-input paths, the upper preview continues to show source data. On joint-analysis paths it shows **Calculation input A/B**: the selected pair's actual inputs, including any completed upstream preprocessing.

Preview runs in the background up to the selected node and does not create database records. Displayed data may be decimated; calculations use complete input. Recalculate requests a new preview and Fit restores the plotted range. Returning from a full run allows further input and node selection. A downstream failure preserves successfully generated source data and shows diagnostics below. For an empty preview, check the selected output, input file, upstream connection and parameter diagnostics.

<a id="run"></a>

## Run and inspect results

Run workflow saves complete outputs with parameters, method versions and diagnostic information. Editing parameters does not modify existing records: run again to create new ones.

The bottom **Steps and outputs** area has two resizable columns. Choose a step on the left and check outputs on the right. Opening a record shows the default output of its last successful graphical step, or the last successful table if no graphical output exists. Clicking a step shows its default output.

An already open primary output is selected with its style preserved. A missing default plot opens on a new figure page. Choose **Settings → Default plot destination → Current figure page** to add it to the active page instead, starting a new page after four plots. Browsing or checking an output does not change the step's default output or this preference. Failed steps with no output show diagnostics and retain existing figures.

Choose one step at a time; checked outputs can span several steps in the same record. Click the checked count to review or remove them. Changing records clears checks; checking outputs does not change the plot destination. Drag the dividers to resize the lists, or collapse them for more plotting space. Reopening restores their previous height.

Choose the destination by clicking a page tab or subplot header/plot. **Add plots** groups related outputs from the same step: spectra with their backgrounds, thresholds and peak markers; wavelet power with its cone of influence (COI). Heatmaps for different quantities, such as raw power, scale-corrected power and significance ratios, remain separate. Other outputs remain independent.

There are four subplots per page and up to 32 pages. The number on **Add plots** shows how many new plots will be created; existing plots remain. For example, selecting all four wavelet heatmaps and the COI adds four plots, each with the COI. Add plots also works on an empty workspace. **More → Plot separately** provides individual-output views.

**Overlay on current** keeps the destination selected when submitted. Ordinary curves require the same data kind and compatible X/Y units. A wavelet step's COI can overlay its own period map. Existing auxiliary curves are not duplicated, and heatmaps for different quantities do not cover each other. Incompatible overlays show a reason and preserve the original figure. Capacity and grouping rules are described in [plotting details (Chinese)](ui/plotting.md). **More** also offers New page and Replace current subplot (one output). Ordinary tables remain separate; cross-spectrum tables provide a metric selector.

Ordinary curves require identical unit strings on both X and Y; an axis with no unit must also have the same name. Overlay does not convert units or require similar ranges or equal sample counts. Check that physical quantities and coordinate references are comparable.

Axes that have not been zoomed, panned or given a manual range fit the combined data after a successful overlay. Manually adjusted axes retain their viewport. **Fit** restores the full range of visible curves.

Right-click **Clear plot** to retain the subplot's position and size. Select the empty slot, check outputs, and choose **Fill current subplot** to draw curves or a heatmap; multiple outputs must be compatible in one subplot. Previous styles are not retained. Loading or failed plots are not empty slots. Clearing a plot does not delete saved results.

Subplot layout is adjustable. Three plots default to two above one full-width plot; four use a two-by-two layout. Hover over the gap to reveal a divider, drag to resize, and release to refresh the plots. Double-click the divider to equalize its two regions. Small windows scroll instead of collapsing plots. Dense legends retain enough subplot space without shrinking fonts or omitting entries.

Drag a subplot title to arrange it: drop at another subplot's center (including its title center) to swap; at its edge to place beside or above/below it; or at the outer page edge to span a full row or column. Check the drop preview before releasing. Esc or an invalid drop cancels the move. The title's context menu offers page-edge moves, swaps and Reset arrangement. Reset uses insertion order. Adding to a custom layout splits the largest region while retaining the others; deleting a subplot expands its adjacent region. Dragging is limited to the same page.

Moving retains data, zoom, styles and the target of any submitted overlay. Switching pages restores layout and proportions. Layout is retained while the page remains open and is not saved in the analysis database. Whole-page PNG/PDF follows the arrangement and proportions; single-plot export fills the page. Changes made after export begins do not affect that file’s layout or styles.

CSV and Use as data source are in the right Export tab and act on the currently browsed step/output. Checks are for plotting, not batch CSV export. PNG/PDF acts on the active subplot or page. Both targets are labelled separately.

Record browsing and figure selection are independent. Changing records preserves open figure pages.

Deleting a source record removes subplots that reference it and cancels related pending plotting operations; other subplots remain. Diagnose the first failed step before downstream failures.

The right sidebar provides Plot, Export and Sources. Plot edits appearance; Export saves files; Sources shows parameters and provenance. The editing target identifies the active subplot. If no subplot is selected, its parameter form is hidden.

On ordinary paths, concurrency limits simultaneously processed inputs. Within a linked joint batch, it limits ready calculation jobs; the default is at most eight and no more than the available logical processors. Saved preferences are retained. Reduce concurrency for large grids or memory-intensive analyses.

<a id="plots"></a>

## Plotting and export

Hover over an axis, its ticks or title for the interaction hint. Scroll to zoom that axis and drag to pan it. Interactions inside the plot affect both axes. Fit restores the data range.

The fixed X/Y selector at the top of the Axes page chooses the editing target. Each axis keeps its own range, title, scale, direction, ticks and grid. Log display requires positive values; ranges are entered in original coordinates. It does not transform stored data.

MTM's frequency upper limit uses inverse input-axis units. It crops the output spectrum, threshold curves and peak table, affecting saved results and downstream data. Estimation, background fitting and peak detection still operate on the full spectrum. Use axis zoom to change only the view. The editor displays numeric `0` with a separate automatic-limit hint; zero selects the automatic limit for new nodes; if an older node retains a legacy ratio, explicitly enter zero and confirm with Enter or Tab to restore the full band.

Result plots refine after first display, zoom, pan or resize; the status below each heatmap reports its current grid resolution. Zoom in for finer detail. Panning alone may still require reduction, and overviews can omit small features. Automatic colour limits use the complete output and remain fixed during refinement. CSV retains complete numerical outputs.

Heatmaps default to Rainbow. Use Plot → Heatmap to adjust the palette, colour limits and display options. CWT period maps and their COI use a logarithmic (base 2) period axis, with small periods at the top. Turn off Reverse direction under Axes → Y axis to change this. Other heatmaps keep their coordinate direction.

Previews, results and figure exports share default styles. Manual settings persist while the page is open and do not change stored values. Closing a page or the application discards its layout and styles. Restore defaults uses the current version's default styles.

Click samples, peaks or grid cells to inspect coordinates. The readout displays 8 significant digits; Copy preserves the current sampled values at full numerical precision. A color and line-style swatch identifies curve readings. Positive frequencies can show reciprocal periods. Decimated preview readings refer to displayed samples. Esc, clicking blank space or changing the view dismisses the readout. CSV exports complete selected outputs; PNG/PDF exports figures and styles subject to display budgets. See [plotting details (Chinese)](ui/plotting.md) and [statistical limits (Chinese)](methods/statistical_limits.md).

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

Press F1 in the main window or choose Settings → User guide. Click a parameter's question mark to open the matching manual section. Method help opens the method's section. The user guide and parameter reference follow the interface language. Chinese entries without a translation use English. Detailed scientific documents are currently in Chinese.

The built-in viewer supports Back, Forward and Find on page. Enter searches forward with wraparound; Ctrl+F focuses the search field. The manual works offline. View on GitHub opens the matching online page. Missing local pages or sections fall back to GitHub; online documentation reflects the repository's current version.

Settings → Contact us opens the authors and contact page. Settings → License opens the offline copyright notice, license terms and source information.

<a id="troubleshooting"></a>

## Troubleshooting and updates

Missing Chinese glyphs usually require a CJK font; Linux/WSL instructions are in the [FAQ](../FAQ.md#linux-fonts). Linux startup also requires a working graphical environment and platform libraries. Evaluate input size, concurrency and graphics separately when investigating slow interaction; WSL performance does not establish native desktop performance.

COCO requires a depth axis. The metre-valued composite-depth labels `mcd` and `rmcd` are accepted with depth unit `m`, preserving the source depth reference. Resampling does not convert age to depth. COCO Monte Carlo allows 19–20000 simulations, subject to a separate fixed budget of 10 billion estimated work units per task. All scan-adjusted p values being one can be a valid result. See the [COCO FAQ (Chinese)](../FAQ.md#coco-units) for interpretation and budget diagnostics.

Settings can check releases and open the download page. This version uses complete-package manual updates without background installation. Keep the database and settings when replacing a program package. Reports should include version, platform, reproduction steps and errors; remove personal paths, credentials and confidential data.
