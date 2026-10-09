# foo_filetree - plan

A folder tree panel for foobar2000 v2, hosted in both Default UI and Columns UI. Browse the disk,
send folders and files to playlists, open things in Explorer. Written from scratch.

Prior art: `foo_uie_explorer` (2005, source lost) and its fork
[GChristensen/foo_uie_explorer_mod](https://github.com/GChristensen/foo_uie_explorer_mod)
(~14k lines, MFC-era Win32). We take feature *ideas* from it, nothing else: no code, no config
format, no GUIDs. Fresh GUIDs everywhere so both can be installed side by side.

## Non-negotiable goals

1. **Performance.** Zero CPU when idle. The main thread never touches the disk. Opening a folder
   with 10,000 entries must not stall the UI. Memory scales with what is *visible/expanded*, not
   with the size of the drive.
2. **Lightweight.** No dependencies beyond the foobar2000 SDK, Columns UI SDK, WTL and Win32.
   No GDI+, no MFC.
3. **Good UI/UX.** Looks native in both UIs, dark mode, per-monitor DPI, keyboard first, sensible
   defaults so most people never open Preferences.
4. **Highly customisable.** Every behaviour the old component hard-coded or exposed as a
   free-text field becomes a proper setting, but grouped so the page stays readable.
5. **Modern C++.** C++23, RAII for every handle, no file over ~600 lines.

## Performance budget

Acceptance criteria, measured on the user's machine with the component's own counters
(panel right-click > Performance, to be built in M7). "Measured" stays empty until it is.

| Metric | Budget | Measured |
| --- | --- | --- |
| Idle (panel visible, nothing changing) | 0 timers armed, 0 repaints, 0 wakeups | |
| Main-thread disk I/O | none, ever (enumeration, attributes, icons all on workers) | |
| Expand a folder with 10k entries | first rows painted < 50 ms after the click; UI never blocked > 8 ms | |
| Paint, full panel 1080 px tall | < 1 ms p99 (only visible rows are drawn) | |
| Memory per node | <= 64 bytes + name (UTF-16, pooled) | |
| Steady-state allocations while scrolling | 0 | |
| Component load (`on_init`) | < 2 ms; the tree is built lazily on first show | |
| File-change handling | debounced (>= 150 ms), only for expanded folders, coalesced into one diff | |

## Architecture

Layers, bottom up. Each one is testable without foobar2000 running.

```text
src/
  platform/   Win32 + fb2k wrappers: handles, threads, strings, config, logging, perf counters
  fs/         enumeration workers, change watcher, icon cache, playable-type filter
  model/      the tree: node pool, flat visible-row list, sort, filter, selection, marks
  view/       the custom tree control: layout, paint, hit-test, keyboard, scroll, drag & drop
  actions/    playlist operations, shell menu, external commands, favourites
  hosts/      CUI panel, DUI element, Preferences pages, context menu
```

### Key decisions

- **Own tree control, not `SysTreeView32`.** A virtualised view over a flat array of visible rows.
  Paint cost is proportional to rows on screen; expanding 10k children is one array splice, not
  10k `TVM_INSERTITEM` messages. It also gives us full control over row height, indentation
  guides, hover/selection colours, dark mode and marks, which the common control fights.
  Cost: we own keyboard navigation, accessibility (UIA provider, later milestone), tooltips and
  in-place rename. Accepted.
- **GDI for painting**, double-buffered into one cached DIB. Text through `ExtTextOutW` with the
  host font (CUI font client / DUI font), which matches the rest of the UI exactly and is faster
  than DirectWrite for short single-line labels. Revisit only if a measurement says so.
- **Enumeration** with `FindFirstFileExW(FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH)` on a small
  worker pool, results posted back in batches. One request per folder, cancellable when it
  collapses or the panel closes. No `SHGetFileInfo` per file on the UI thread.
- **Icons**: system image list indices looked up per *extension* (`SHGFI_USEFILEATTRIBUTES`, no
  disk hit) and cached; drives and folders with custom icons resolved on a worker. Option for
  our own vector glyphs instead (also how custom folder icons are done - never by patching the
  shared system image list like the mod did).
- **Playable filter** built once from fb2k's registered input types (and playlist types), as a
  hashed extension set. Re-read only when Preferences change. Archives and cue sheets handled
  through it too.
- **Watching**: `ReadDirectoryChangesW` (overlapped, one shared IOCP thread) for expanded folders
  only, removed on collapse. Network drives fall back to refresh-on-expand.
- **Playlist work** goes through `playlist_incoming_item_filter_v2::process_locations_async`, so
  recursion, sorting and tag reading happen off the main thread with fb2k's own progress UI.
- **State**: per-instance config (expanded set, scroll, selected path) through CUI
  `get_config`/`set_config` and DUI `ui_element_config`, versioned and tolerant of bad data.
  Global settings in `cfg_var`s.
- **Sorting**: natural order (`CompareStringEx` with `SORT_DIGITSASNUMBERS`) by default, folders
  first; cached sort keys so re-sorting never re-reads the disk.

## Settings

Old = the mod's preferences (all 28 cfg vars plus the action map). New = what we ship. Grouped
by Preferences tab.

### General
| Setting | Old | New |
| --- | --- | --- |
| Startup location | nothing / last path / fixed path | same three + "now playing folder"; per panel instance |
| Expand target on jump | checkbox | always on, with "also scroll to centre" |
| Roots | drives, favourites before/after drives | ordered root list: This PC drives, favourites, Music library folders, custom folders; each can be hidden |
| Hidden drives | free text `ADE` | checklist of detected drives |
| Single panel instance | forced | multi-instance, each with its own state |

### Display
| Setting | Old | New |
| --- | --- | --- |
| Tree lines | checkbox | none / connector lines / indentation guides; thickness (1-4 DIP, DPI-scaled) and colour (follow text colour at a set opacity, or a custom colour; CUI colour client entry) |
| Icons | checkbox | none / system icons / built-in glyphs; custom folder icon file |
| File extensions | checkbox | checkbox, plus "only for non-audio files" |
| Hidden / system files | checkbox (hidden only) | two checkboxes |
| Address bar | checkbox, frame style | none / path box / breadcrumb, with autocomplete |
| Horizontal scrollbar | checkbox | "wrap / ellipsis / horizontal scroll" |
| Tooltips | checkbox | off / full path / title-format string (file count, size, duration later) |
| Node height | pixel number | row padding in DIPs (DPI-correct) |
| Minimum height, frame styles | numbers, style dialog | dropped (hosts handle this); edge style only for CUI |
| Colours | text + background via CUI | text, background, selection, hover, marked, lines, folder-name, file-name; CUI colour client + DUI colours; dark mode |
| Fonts | none | CUI font client / DUI font |
| Status bar | item count, length | optional footer: item count, selection size, total duration (computed lazily) |
| Sorting | "force items sorted" | name natural / name / date / size / type; folders first; reverse |

### Filter
| Setting | Old | New |
| --- | --- | --- |
| Show files | all playable except / only these (free text `a\|b`) | all files / playable only / folders only, plus exclude and include lists edited as tag lists |
| Don't send to playlist | free text | same list, as tag list |
| Name filter | none | live filter box (Ctrl+F) in the panel; optional wildcard/glob patterns to always hide (e.g. `*.cue` duplicates, `@eaDir`) |
| Empty folders | always shown | option: hide folders with no playable content (computed lazily on a worker) |

### Actions
| Setting | Old | New |
| --- | --- | --- |
| Mouse bindings | left/double/middle + Ctrl/Alt/Shift/Win -> action list | single click, double click, middle click and Enter, each with its own action for folders and for files; every gesture can be set to "None". Single click always selects; its action (default None) runs in addition. Defaults: double click = expand/collapse on folders, play in the temp playlist on files; middle click = add to active playlist; Enter = same as double click |
| Keyboard bindings | per-panel hotkeys | per-panel keys + fb2k main-menu commands so global keyboard shortcuts work |
| Action list | ~45 combinations | one action = target (active / default / named / new / "Tree view" temp playlist) x mode (replace / add / insert) x play (yes / no) x recursive (yes / no / per setting) |
| Recursive by default, Shift overrides | checkbox | same |
| Send to default playlist, Ctrl overrides | checkbox + name | same, playlist picked from a list or typed |
| Temp playlist ("Explorer View") | fixed name | name configurable; optional "follow selection" (auto-fill on select, debounced) |

### Context menu
| Setting | Old | New |
| --- | --- | --- |
| Items | fixed list | choose which items appear and their order; Shell submenu; fb2k context menu for files |
| External commands | app, params with `:P :D :N :T :E`, work dir, window size, group | same, plus title-format-free placeholders documented in the dialog and multi-selection (one run per item / all at once) |
| Favourites | list + group, add from menu | same, plus drag to reorder, rename in place |
| Marks | toggle / children / siblings / to here / unmark | replaced by real multi-selection (Ctrl/Shift click, Ctrl+A in folder) plus optional checkboxes mode |
| Rename, Delete (to Recycle Bin), Open in Explorer, Copy path | yes (no copy path) | yes, all |

### New features (not in the mod)
- Default UI support; dark mode; per-monitor DPI.
- Live filter box and type-ahead search.
- Drag & drop out to playlists and Explorer; drop folders onto the panel to add favourites.
- Highlight / jump to the now-playing file's folder; optional auto-follow.
- Auto refresh on disk changes.
- Library folders as roots; mark folders that are in the Media Library.

## Milestones

Each ends with a build that loads, and a short note in `QUEUE.md`. Runtime checks are done by the
user (we cannot run foobar2000).

| # | Scope |
| --- | --- |
| M0 | Project skeleton: vcxproj, build.bat, component identity, empty CUI panel + DUI element that paint the host background. Loads in both UIs. |
| M1 | Model + workers: node pool, async enumeration, drives as roots, natural sort, playable filter. Unit tests for model and sort. |
| M2 | Tree view: virtualised paint, scrolling, expand/collapse, keyboard, mouse, DPI, colours, fonts, dark mode. |
| M3 | Actions: action model (target x mode x play x recursive, plus None / expand-collapse), configurable single / double / middle click and Enter bindings for folders and files (stored in cfg vars now, edited in M5), playlists via `process_locations_async`, Shift/Ctrl overrides, temp playlist. |
| M4 | Context menu: own items, shell menu, fb2k context menu, rename, delete, copy path, open in Explorer. |
| M5 | Preferences pages (General, Display, Filter, Actions, Menu & Commands, Favourites). Display includes tree lines / indentation guides (style, thickness, colour), drawn per visible row with the DC pen (no allocation). Actions includes the click-binding editor. Run `scripts/dialog_check.bat` after every layout change. |
| M6 | Drag & drop out (playlists, playlist tabs, Explorer; user request, done first), address bar / breadcrumb, filter box, favourites as roots, startup modes, per-instance state, type-ahead find, main-menu commands (bindable in foobar2000 keyboard shortcuts; optional Total Commander preset F5-F8). |
| M7 | Change watching, now-playing follow + playing marker, footer status, performance counters, drop onto the panel; file operations via Windows (`IFileOperation`: new folder, copy, move with its progress / cancel / conflict UI), delete confirmation with file and folder count for recursive deletes, Open in Explorer, Properties, Open with, save folder as playlist (with recursion), "add to queue" action, user-defined external commands (`%f` file, `%d` folder), library indicators and "add folder to Library" (if the SDK allows). **Scope cut (user, M7):** the Explorer submenu already covers cut/copy/delete/properties/open with; ours is only New folder (F7), dropping onto the panel (copy, Shift move) and Ctrl+X/C/V through the shell clipboard. No Total Commander F5/F6 (no second pane to target). |
| M7d | Multi-select (user): Ctrl+click toggle, Shift+click / Shift+arrows range, Ctrl+A, Ctrl+Space; selection kept through merges/splices/filter; play / send / queue / save playlist, Copy/Cut, Delete, drag out, Copy path, fb2k and Explorer submenus act on the whole selection; Rename, New folder, Properties of one item stay single. |
| M8 | Done except the Fonts tab (deferred, see QUEUE.md M8f). Polish: accessibility (UIA; done as MSAA, which UIA clients reach through Windows' proxy), tooltips, zebra stripes and hover colour options, optional Fonts tab with fallback families (fb2k-common `fbc::fonts`; needs text drawn through DirectWrite via `IDWriteBitmapRenderTarget` instead of GDI, so only if measured cost stays inside the paint budget), x86 build, packaging, README, release. |

Community suggestions (IloveFb2k, 2026-10): only the cheap items that fit a fast folder tree were
adopted (in M6-M8 above). Tabs, list / grid views, columns, info pane, tag search, scripting
hooks, streaming servers and similar were declined as out of scope.

## Risks

- **Own tree control = own accessibility.** Screen readers see nothing until the UIA provider in
  M8. Acceptable for a first release only if called out in the README.
- **Network and slow drives.** Enumeration must be cancellable and never block shutdown; use
  `fb2k::mainAborter()`-aware workers and time-outs on `FindFirstFile` for offline shares.
- **Shell context menu** (`IContextMenu`) runs third-party shell extensions in-process; some are
  slow or crash. Built only on demand, on the main thread (COM STA requirement), never cached.
- **Change notifications** can flood (bulk copies); debounce and coalesce, cap per folder.

