# Aster 1.3.2-beta.7 User Guide

[Classified contents](README_EN.md)

Data-processing guidance: [Smoothing and detrending](methods/smoothing_trends_en.md) · [Standardization and units](concepts/data_processing_en.md#centering) · [Outliers and transformations](methods/outliers_and_transforms_en.md) · [Filtering and envelopes](methods/filter_usage_en.md).

[中文手册](USER_GUIDE.md) · [Task and parameter reference](reference/task_parameters_en.md) · [Methods (Chinese)](methods/README.md) · [FAQ (Chinese)](../FAQ.md)

## Download and start

**No installation is required. Extract the complete package and launch Aster.** On Windows, open `Aster.exe`; on macOS, open `Aster.app`; on Linux, run `./AppRun`. Download the application package for your operating system and keep all its files together. Source archives are intended for development and compilation.

If Windows reports missing `VCRUNTIME` or `MSVCP` files, install the [Microsoft Visual C++ x64 runtime](https://aka.ms/vc14/vc_redist.x64.exe). Linux requires compatible system libraries, X11/XWayland and fonts. macOS packages require macOS 15+ and the correct processor architecture; signing information is provided with the release. See the [startup FAQ (Chinese)](../FAQ.md#portable).

<a id="overview"></a>

## Workspace

Build a task tree on the Tasks page, inspect the selected node's output preview, then run the workflow to save results. The Results page provides database browsing, figures and export. On a wide window, the live preview starts at about one third of the window width. Drag its right edge to give the source and output plots more room, or click the grip to collapse it. Language, theme and concurrency are configured in Settings.

<a id="inputs"></a>

## Input data

Import delimited CSV, TXT, TSV or DAT files. Select a file and verify the delimiter, header, X/Y columns and units. Changes apply automatically to selected files. Bulk import settings copy column positions, not header names. Each file is checked; incompatible files retain their previous settings and are listed with reasons. Import and processing do not overwrite source files. Hover over a file for its full path and status.

See the [data-source guide](methods/data_sources_en.md) for files, saved outputs, test signals and ODP records.

Built-in sources include sine signals, synthetic orbital proxies, ODP records and astronomical curves. Independent sources are not repeated for each imported file. Check the age/depth axis, provenance and sampling of ODP data. Coordinate units are metadata; changing a unit label does not convert the numbers. Use an age-model task to transform depth to age.

<a id="input-selection"></a>

### Input-list shortcuts and selection scope

The input and result lists use a plain background. Hovering shades a row gray. In the input list, blue-gray shading identifies the selected files and a short bar marks the file being browsed. The dot next to the filename shows its run status. Hover over a file for details. Removing a file and importing the same path creates a new pending input. Undoing the removal restores the original input and its run status. During a run you can scroll and browse the list. Expand Columns and import settings to edit, then drag the divider to resize the list.

Click **?** next to Move/Remove to open this section. Focus the file list first.

- Windows/Linux: Ctrl+A selects all; Ctrl+click toggles individual selections; Shift+click selects a range.
- macOS: Command+A selects all; Command+click toggles selections; Shift+click selects a range.
- Clicking a file selects the preview. Editing Columns and import settings applies the current delimiter, header, X/Y positions, units and skip-invalid-rows options to selected files. Units are labels, not numerical conversions.
- Move and Remove operate on selected inputs. Remove does not delete disk files. Wait for bulk validation before moving inputs or running the workflow.
- Selection controls only left-side initialization/import and list operations. **It does not batch-edit right-side task parameters.** Those still belong to the current data/pair; use Apply to all data in this workflow in the separated right footer to unify one node.

Incompatible files retain their settings with an explanation. Returning a field to its original value before committing preserves each selected file’s own settings. Effective edits are undoable and processed in order. Changing selection does not retarget pending edits. If the focused row is deselected, select it before editing import settings.

<a id="workflow"></a>

## Tasks and parameters

Click the round plus on the right of a node to choose a downstream task. Each node supports eight direct children. New siblings appear from left to right on the next row. You can also select a parent and double-click a task in the catalogue, or drag a task onto the parent’s plus button. Select the appropriate upstream output when a task has several outputs. Delete selected nodes with the toolbar or Del and check the confirmation. Nodes, connections, parameters, imported file lists, file import options and pairs share one undo/redo history, limited to 100 effective edits and a conservative 64 MiB metadata budget. Older history may expire sooner when the budget is reached. Undoing a node deletion restores its source bindings and pairs; undoing input removal restores the original dataset identity. Undo does not restore file contents on disk or remove saved results. Preserve original data separately. Workflow editing is disabled during a run.

When several compatible outputs are available, the chooser opens near the source node. Rows have a consistent height and the default output is bold. More than eight choices enable search and scrolling. Hover for the full name; click or press Enter to confirm. Escape or clicking outside cancels without adding a task. A single compatible output connects directly.

Click the small “+” on an existing connection, or drop a task there, to insert a step. To replace an ordinary node, use its context menu or drop another task onto the node. When the input and every downstream connection have exactly one compatible output, the edit is applied immediately without a selection dialog. Otherwise, the review dialog lists the new input and the output for each downstream branch. Check their meaning: compatible data types do not imply equivalent scientific content. Ambiguous outputs require an explicit choice. The new method starts with its own defaults; adjust them in Step settings after the edit. Replacement preserves downstream branches; insertion preserves existing nodes.

When a choice is needed, nothing changes before confirmation and Cancel leaves the workflow untouched. One undo restores either an immediate or a confirmed insertion or replacement. Source nodes, joint nodes and edits directly involving joint inputs or outputs are not enabled. Inserting into a legacy directional connection requires room for its downstream subtree in the logical grid.

Valid parameter changes update the preview. Decimal parameters in Step settings accept scientific notation such as `1e-15`, within the declared range. Confirm with Enter or Tab; changing nodes or running the workflow also commits the current edit. A numeric editor accepts wheel changes only after deliberate click or Tab focus. A closed choice list ignores wheel selection. FFT, Welch and MTM require suitable regularly spaced input; inspect sampling and resample explicitly when necessary, or consider Lomb–Scargle for irregular sampling. Compare detrended and original branches when investigating long trends.

For continuous wavelet analysis, choose the calculation convention by its power definition. The default is variance-normalized, scale-corrected power; the alternative is raw power (Torrence–Compo). Their scale ranges and AR(1) estimates also differ. This choice affects computation, while log axes, reversed axes and colors only affect plotting. See the [wavelet method guide (Chinese)](methods/cwt.md).

## Sampling inspection and resampling

Use Sampling and gap inspection to inspect order, duplicate X and intervals without modifying the series. Use Sort and deduplicate for cleanup only, and Sort, deduplicate and resample when a new grid is required. No-resampling mode still sorts and deduplicates. See [task instructions](methods/preprocessing_en.md) for outputs and endpoint behavior, and [shared sampling concepts](concepts/sampling_en.md) for spacing and interpolation guidance.

<a id="evolution-spectrum"></a>
## Evolutionary FFT and MTM

Evolutionary spectra estimate power in complete moving windows to describe changes with time or depth. Choose a research scale and window length first, then position density, FFT length and normalization. Input must already be strictly increasing and uniform; no preprocessing is inserted automatically.

See the [task guide](methods/power_spectra_en.md#evolution-settings) for setup and outputs, and [shared parameters](concepts/spectral_parameters_en.md) for windows, tapers, padding, units and display mapping.

### Evolutionary spectrum preset and resolved settings

New evolutionary FFT tasks already use the preset by default, without a separate Apply action. It is an editable starting configuration, not a universal recommendation. It changes both calculation and display settings. See [preset details](methods/power_spectra_en.md#preset) for values, sources and cross-software comparisons.

Resolved length, step, overlap and FFT length appear between the lower node-output selector and its plot after a successful preview. This is output-preview information, not the right-hand parameter editor or the Results page. Hover for counts, spans and tail information; also inspect the calculation report for reproduction.

<a id="parameter-scope"></a>
## Parameters for each dataset

| Operation | Scope |
| --- | --- |
| Left import settings | Reading configuration for selected files |
| Right node parameters | Current dataset or pair at the selected node |
| Right batch-settings button | All datasets or pairs at this node in the current workflow |
| Results plot style | Display of the current plot object; no database-value changes |

Select an input and a task node. Parameter edits apply to that dataset at the selected node; joint nodes and their descendants use the selected pair. A fixed footer identifies the scope and provides **Apply to all data in this workflow** (or **Apply to all pairs in this workflow** for paired tasks) without scrolling through long forms. It unifies this node’s settings within the current analysis workflow and sets defaults for future inputs in that workflow. Undo restores the prior individual settings.

### Automatic settings and restoring defaults

Opening the editor does not determine parameters. Each node has base settings; each dataset or pair can override them.

| State | Effective settings |
| --- | --- |
| Never run or never opened in the editor | Node base settings plus any saved input-specific overrides |
| Open or switch the editor only | Read effective settings; do not write overrides or resolved counts |
| Edit the current dataset | Store differences from the node base settings for that input |
| Apply to all datasets/pairs in this workflow | Unify this node’s base rules and clear the corresponding individual overrides |

Automatic values are resolved during each preview and run from **this dataset’s actual upstream output**. Changes to input length, spacing or upstream processing trigger new resolution. Batch application copies rules; intentionally fixed sample counts remain fixed. Other nodes and workflows are unaffected.

| Setting | Automatic behavior |
| --- | --- |
| Evolutionary window, step and FFT length | Select span percentage, target window count/overlap and window-length multiple |
| Resampling interval | Select mean, median, minimum or maximum spacing instead of fixed spacing |
| FFT length or frequency fields labelled “0 = automatic” | Enter 0; the task resolves it using its own definition and input |
| Parameters with estimation switches, such as AR(1) coefficient | Enable the estimation option; each input is estimated separately |

**Restore default parameters** resets all defaults at this node for the current dataset/pair, including automatic modes. It retains point-level outlier decisions. To restore just one automatic setting, use its selector or zero value. To restore this node across the workflow, reset first and then use the batch button. Parameter edits support undo. Opening an old workflow does not replace explicitly saved settings.

Batch application preserves each existing input’s point-level keep/exclude decisions; future inputs do not inherit those decisions. If the source dataset or pair has been removed, select a valid input before applying settings.

Preview failures show the main reason and suggested action in the node-output plot area in larger text. Long messages scroll and can be copied without shrinking the font.

Point-level keep/exclude decisions are never copied to other inputs. Applying detection rules retains each dataset's point decisions. Changes to input values invalidate old point decisions and require review.

Use **Move** beside Remove to send selected files to an existing or new tree. Destination and **Copy processing steps** are separate choices. Existing destinations use their own processing settings; copying steps to a new tree retains the selected data's node settings. Input identities and import options remain unchanged. Existing pair references are not silently retargeted; copied joint nodes need new pair configuration. The move can be undone.

## Outliers and value transforms

**Data processing → Cleanup → Outlier detection and handling** offers Hampel, global MAD and Y bounds. The default only marks points unless manual exclusions are present. Exclusion or local-median replacement must be selected explicitly; original samples and a point decision log are retained. In the node preview, right-click a curve to keep or exclude an individual point; manual exclusion still applies in mark-only mode. Direct entry of replacement values is not provided. Genuine geological events may produce extreme values.

**Data processing → Amplitude → Value transformation** offers ln, log10, asinh and tanh. These change amplitude interpretation and downstream statistics; they do not classify outliers. Logarithms require positive values. asinh accepts signed values; tanh saturates. Missing intervals and hiatus filling are not inferred automatically.

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

During a batch, you can open and filter saved results. The directory keeps your reading position. Use “Refresh · New results” to see the latest saves. Cancelling stops dispatching further files. The window can close once active inputs have finished saving. Deletion, metadata editing and database management become available when the run ends.

Run workflow saves the outputs actually produced by tasks, with parameters, method versions and diagnostics. It cannot restore content explicitly omitted by a method. For example, sampling-detail row limits differ from preview decimation; see [display and output limits (Chinese)](ui/live_preview.md#display-limits). Editing parameters does not modify existing records: run again to create new ones.

The bottom **Steps and outputs** area has two resizable columns. Choose a step on the left and click output rows on the right. Opening a record shows the default output of its last successful graphical step, or the last successful table if no graphical output exists. Clicking a step shows its default output.

An already open primary output is selected with its style preserved. A missing default plot opens on a new figure page. Choose **Settings → Default plot destination → Current figure page** to add it to the active page instead, starting a new page after four plots. Failed steps with no output show diagnostics and retain existing figures.

Click a row to select one output in the current step. Ctrl-click (Command-click on macOS) to add or remove individual rows. Shift-click to select a range. Selections persist across steps in the same record. Click the selected count to review or clear them. Changing records clears the selection. Drag the dividers to resize the lists, or collapse them for more plotting space. Reopening restores their previous height.

Choose the destination by clicking a page tab or subplot header/plot. **Add plots** groups related outputs from the same step: spectra with their backgrounds, thresholds and peak markers; wavelet power with its cone of influence (COI). Heatmaps for different quantities, such as raw power, scale-corrected power and significance ratios, remain separate. Other outputs remain independent.

There are four subplots per page and up to 32 pages. The number on **Add plots** shows how many new plots will be created; existing plots remain. For example, selecting all four wavelet heatmaps and the COI adds four plots, each with the COI. Add plots also works on an empty workspace. **More → Plot separately** provides individual-output views.

**Overlay on current** keeps the destination selected when submitted. Ordinary curves require the same data kind and compatible X/Y units. A wavelet step's COI can overlay its own period map. Existing auxiliary curves are not duplicated, and heatmaps for different quantities do not cover each other. Incompatible overlays show a reason and preserve the original figure. Capacity and grouping rules are described in [plotting details (Chinese)](ui/plotting.md). **More** also offers New page and Replace current subplot (one output). Ordinary tables remain separate; cross-spectrum tables provide a metric selector.

Overlay requires matching X and Y unit strings. If X units are unspecified, the X names must also match. For an explicit series overlay with both Y units unspecified, different Y names alone no longer block the operation; this does not establish physical equivalence. Spectra retain their stricter axis check. No units are converted automatically, and sample counts need not match. Confirm comparable quantities and coordinate origins before overlaying.

Axes that have not been zoomed, panned or given a manual range fit the combined data after a successful overlay. Manually adjusted axes retain their viewport. **Fit** restores the full range of visible curves.

Right-click **Clear plot** to retain the subplot's position and size. Select the empty slot and the desired output rows, and choose **Fill current subplot** to draw curves or a heatmap; multiple outputs must be compatible in one subplot. Previous styles are not retained. Loading or failed plots are not empty slots. Clearing a plot does not delete saved results.

Subplot layout is adjustable. Three plots default to two above one full-width plot; four use a two-by-two layout. Hover over the gap to reveal a divider, drag to resize, and release to refresh the plots. Double-click the divider to equalize its two regions. Small windows scroll instead of collapsing plots. Dense legends retain enough subplot space without shrinking fonts or omitting entries.

Drag a subplot title to arrange it: drop at another subplot's center (including its title center) to swap; at its edge to place beside or above/below it; or at the outer page edge to span a full row or column. Check the drop preview before releasing. Esc or an invalid drop cancels the move. The title's context menu offers page-edge moves, swaps and Reset arrangement. Reset uses insertion order. Adding to a custom layout splits the largest region while retaining the others; deleting a subplot expands its adjacent region. Dragging is limited to the same page.

Moving retains data, zoom, styles and the target of any submitted overlay. Switching pages restores layout and proportions. Layout is retained while the page remains open and is not saved in the analysis database. Whole-page PNG/PDF follows the arrangement and proportions; single-plot export fills the page. Changes made after export begins do not affect that file’s layout or styles.

CSV and Use as data source are in the right Export tab and act on the currently browsed step/output. Add plots uses the outputs selected across steps. CSV exports the single output chosen in the Export tab. PNG/PDF acts on the active subplot or page. Both targets are labelled separately.

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

Click samples, peaks or grid cells to inspect coordinates. The readout displays 8 significant digits; Copy preserves the current sampled values at full numerical precision. A color and line-style swatch identifies curve readings. Positive frequencies can show reciprocal periods. Decimated preview readings refer to displayed samples. Close an individual bubble with its close button or Esc. Blank clicks, page changes, panning and zooming preserve bubbles; off-screen points temporarily hide them. The plot context menu clears that plot, and the results toolbar clears the current page. Figure exports omit bubbles by default. CSV exports complete selected outputs; PNG/PDF exports figures and styles subject to display budgets. See [plotting details (Chinese)](ui/plotting.md) and [statistical limits (Chinese)](methods/statistical_limits.md).

<a id="database"></a>

## Database and selection

SQLite stores results. Changing the database path does not copy or delete records in the previous database. Use the Database menu for backup and merging compatible Aster databases. Imported records receive new IDs; incompatible schemas are rejected.

Click a file name to browse it. Use the checkboxes for batch actions. With the list focused, move to a row using the arrow keys. Enter opens that record and Space toggles its check. The footer shows selected records / all records in this database. Select all records and Deselect all records act on the whole database. Select matches and Deselect matches act on all pages of the current filter. Each button remembers its own next action across row edits, filter changes and paging. After selecting the whole database, selecting matches leaves the count unchanged. Clicking Deselect matches then removes just that group. Delete checked confirms the full selection and the count outside the current filter.

The results directory marks the open record with blue-gray shading and a short bar at the left. Right-click a row and choose Check this record, or press Space to check the first record. Checkboxes then share the leading slot normally used for status. Clearing the last check restores the status indicators. Filenames stay aligned throughout. The marker stays with the open record when you check another file or move keyboard focus. Hover for ID, date and source. Filters opens the ID lookup and peak bounds. The default is period. Switch to frequency to enter frequency bounds. A period range queries reciprocal frequency within the selected unit. Undeclared units form a separate group. Click a date or peak-range chip to clear it. The filter searches records for matching peaks. Use Axes in the figure inspector to set the plot’s visible range. Successful records have a green dot before the name. A record with a workflow error has a single red exclamation mark at the right. Hover for its run status. In Steps and outputs, Failed identifies the step with the error. Skipped means an upstream step did not supply its input. Select the skipped step to inspect the cause. Warnings and cancellation alone leave the row unmarked.

Export filenames writes the IDs and original names of all checked records to CSV, including checks across pages and filters. Generated data uses the source name. Use the right-hand Export tab for numerical outputs or to reuse a saved output. See [saved-output reuse](methods/data_sources_en.md#database).

<a id="shortcuts"></a>

## Keyboard shortcuts

Buttons and panel grips include shortcuts in their tooltips. The table uses Windows/Linux notation; the interface displays native key names.

| Key | Action |
| --- | --- |
| F1 | Open the user guide, including from Settings |
| Ctrl+, | Open Settings |
| Ctrl+1 / Ctrl+2 | Switch to Tasks / Results |
| System Undo / Redo shortcuts | Undo or redo workflow edits on the Tasks page |
| Ctrl+Shift+1 | Toggle the preview panel |
| Ctrl+Shift+2 | Toggle step settings |
| Ctrl+Shift+3 | Toggle the task catalogue |
| Esc | Close the current dialog or dismiss a plot readout, depending on the active control |

Panel shortcuts apply only on Tasks. Main-window navigation does not activate through modal dialogs.

<a id="help"></a>

## Help

The collapsible left-hand contents tree groups Getting started, Features, Shared concepts and parameters, Troubleshooting, and About and references. Task-specific defaults appear under Features → Task parameter reference. Shared sampling, spectral and statistical topics have one authoritative page linked from multiple tasks. Opening method or parameter help expands and selects the matching chapter. The path above the document shows its location; Home returns to this guide.

| Purpose | Action |
| --- | --- |
| Search all built-in help | Use Search all help on the left; select a result to open its section |
| Find a word on the current page | Click Find in page or press Ctrl+F; Enter/Next moves forward and Previous moves backward |
| Focus full-text search | Ctrl+Shift+F; Command+Shift+F on macOS |
| Return to a reading position | Back/Forward preserves the scroll position when leaving a page |
| Return to the contents tree | Clear the left search box; the document and history remain |

Full-text search is case-insensitive. Space-separated terms must all occur in the same section. Parameter identifiers such as `evo_target_windows` are searchable. Only compiled help is indexed, not external online material. Search uses the interface language, with untranslated built-in material retained and marked “中文” where appropriate. The first search builds the index in small batches with progress feedback. Matching terms are highlighted in the displayed article. Editing or clearing the query removes the old highlights, and opening another article updates them for that page. Clearing the query or closing the window stops pending search work.

Find in page shows the current match number and total, with wraparound. F3/Shift+F3, or the platform’s Find next/previous shortcuts, also move between matches. This is independent of the left-hand search. Hover for full titles; drag the divider to resize the contents pane.

Press F1 in the main window or choose Settings → User guide. Parameter question marks open the matching parameter section; method help opens the method section. The built-in manual works offline. View on GitHub opens the matching online page. Missing local pages or sections fall back to GitHub; online documentation may be newer than the installed version.

Settings → Contact us opens the authors and contact page in the built-in offline manual. Settings → License opens the offline copyright notice, license terms and source information.

<a id="troubleshooting"></a>

## Troubleshooting and updates

Missing Chinese glyphs usually require a CJK font; Linux/WSL instructions are in the [FAQ](../FAQ.md#linux-fonts). Linux startup also requires a working graphical environment and platform libraries. Evaluate input size, concurrency and graphics separately when investigating slow interaction; WSL performance does not establish native desktop performance.

COCO requires a depth axis. The metre-valued composite-depth labels `mcd` and `rmcd` are accepted with depth unit `m`, preserving the source depth reference. Resampling does not convert age to depth. COCO Monte Carlo allows 19–20000 simulations, subject to a separate fixed budget of 10 billion estimated work units per task. All scan-adjusted p values being one can be a valid result. See the [COCO FAQ (Chinese)](../FAQ.md#coco-units) for interpretation and budget diagnostics.

Opening Settings → Check for updates checks the selected channel once. Stable and Beta are separate; Beta builds initially select Beta. Release notes support expandable details, and diagnostic information stays collapsed until requested. When package information is available, choose a destination and select Download and extract. Aster downloads the full package over official HTTPS, verifies SHA-256 and extracts into a new folder. Automatic source selection may retry an identical package from another listed source; a manually selected source does not switch. Existing destination folders are never replaced. Temporary files are cleaned after completion. Open the folder, close the old application and start the new version yourself. Older releases without package information keep the browser download button. Alpha builds are developer-only and do not participate in online version checks. Keep the database and settings when replacing a program package. Reports should include version, platform, reproduction steps and errors; remove personal paths, credentials and confidential data.
