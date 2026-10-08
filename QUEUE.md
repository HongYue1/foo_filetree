# QUEUE - current task only

Interruption-safe handoff. A new thread should be able to read this file, then `AGENTS.md`, and
continue. Update it *while* working (after each finished step), not at the end. When a task is
done, replace this file's contents with the next task from `PLAN.md`.

## M0 (done, user-tested)

M0 (project skeleton, empty CUI panel + DUI element) is built and committed (`1b662d2`). The user
has not yet confirmed it loads. Check: loads; panel in CUI Layout > Panels; element in DUI
(Utility); colours and dark mode follow live; two instances work.

M0 findings:
- No `initquit` yet on purpose (2 ms on_init budget, tree built lazily).
- `dumpbin` is not on PATH in Git Bash and `cmd //c "\"...vcvars64.bat\" && dumpbin"` fails on
  quoting. Call it by path: `ls -d "/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/"*/bin/Hostx64/x64`.

## Done: M1 - model + workers (`2577e75`)

`model/` (sort, extension_set, name_pool, tree), `platform/worker_pool`, `fs/` (enumerate,
enumeration_service, drives, fb2k_glue with the only `initquit`). Offline tests:
`cd test && cmd //c build_tests.bat` -> `test/tests.out`, 84 checks pass. Numbers (dev machine):
`sizeof(Node)` 48 B; 10k children apply 0.6 ms, collapse+expand 0.07 ms, 865 KB for 10k nodes;
enumerate+filter+sort 10k files 24 ms on a worker (warm cache).

M1 findings:
- `CompareStringOrdinal` / `CompareStringEx` return 0 (failure) for an empty / null string, which
  silently reads as "equal". `sort.cpp` orders empties itself.
- `fb2k::inMainThread` after `on_quit` is avoided: the glue's poster drops work once quitting.
- Detached workers at shutdown (stuck on a dead share) still hold DLL code. fb2k does not unload
  components before process exit as far as we know; unverified.
- `Tree::build_path` handles at most 256 levels.
- Archives are not in the playable set yet (M3).

## Current task: M2 - tree view

Goal: the panel shows drives as roots and browses the disk. Virtualised paint over
`Tree::rows()`, scrolling, expand/collapse (async loading through `fs::enumeration()`), keyboard,
mouse, per-monitor DPI, host colours and fonts, dark mode. Same view in both hosts.

Plan:
- `view/tree_view.{h,cpp}` - owns a `model::Tree`, outstanding tickets per node, scroll position,
  focus/selection row, hover row. Host-agnostic: hosts pass a `ViewTheme` (colours, HFONT,
  dark) and forward messages. No allocation in paint/scroll.
- `view/layout.h` - row height from font metrics + padding (DIPs), indent per depth, expander box.
- `view/paint.cpp` - one cached DIB (resize only on WM_SIZE), paint rows intersecting rcPaint,
  `ExtTextOutW` with the host font, expander glyphs (GDI lines/triangles), selection/hover fill,
  ellipsis via `DrawTextW(DT_END_ELLIPSIS)` only when the name does not fit (measure cached?).
- Scrolling: `WS_VSCROLL`, `SetScrollInfo`, `ScrollWindowEx` on wheel/keys, dark scrollbars via
  `SetWindowTheme(DarkMode_Explorer)` (already set by the hosts).
- Keyboard: Up/Down/PgUp/PgDn/Home/End, Left (collapse / go to parent), Right (expand / first
  child), `*` / `+` / `-` on numpad, type-ahead later (M6).
- Mouse: click selects, click on expander or double-click on folder toggles, wheel scrolls,
  hover tracking with `TrackMouseEvent` (no timers).
- DPI: `GetDpiForWindow`, re-layout on `WM_DPICHANGED_AFTERPARENT`.
- CUI: font client (new GUID) + selection colours in the colour client. DUI: `query_font_ex`,
  `ui_color_selection`/`highlight`.

Status: **user-tested 2026-10-08: everything works except two key issues, fixed, awaiting
re-test.** (M0 load test also passed: both UIs, two instances, dark mode, colour/font pages.)
- Enter did nothing. Likely cause: Enter is a dialog key, and the host's dialog navigation ate
  it because WM_GETDLGCODE did not return DLGC_WANTMESSAGE for it. Fixed; if the re-test passes,
  record it in the columns-ui-sdk / foobar2000-component-dev skill. (Enter on a file still does
  nothing by design until M3.)
- PgUp/PgDn behaved like Home/End. Probably the list was shorter than the panel, so one page
  reached the end. Now Explorer-style anyway: first to the edge of the view, then a page at a time.
- "(unavailable)" untested: the user has no offline drive.

Steps:
- [x] `view/tree_view.{h,cpp}` (state, layout, scroll, input, async loading) and
      `view/tree_view_paint.cpp` (cached DIB, rows in rcPaint only, DC brush/pen, DrawTextW
      ellipsis); `view/theme.h` (colours + blend helpers). Layout lives in `TreeView::Metrics`
      (no separate layout.h): row = tmHeight + 2x3 DIP, indent 16 DIP, expander 8 DIP triangle.
- [x] Hosts forward messages; roots = drives ("C:" shown, stored "C:\").
- [x] Expand/collapse via expander click, double-click, Enter, Left/Right, numpad +/-. Loading
      dims the expander; a failed listing shows "(unavailable)" and retries on next expand.
- [x] Keyboard: arrows, PgUp/PgDn, Home/End. Unused keys go to fb2k shortcuts
      (CUI `g_process_keydown_keyboard_shortcuts`, DUI `keyboard_shortcut_manager_v2`).
- [x] CUI font client (new GUID) + all six selection colours; DUI uses the CUI font when CUI is
      installed, else `ui_font_lists`; DUI selection text picked by contrast.
- [x] Per-monitor DPI: `GetDpiForWindow` looked up at run time (Win7-safe), font scaled from
      system DPI, re-measure on `WM_DPICHANGED_AFTERPARENT`.
- [x] Build, commit.
- [ ] User test (list below), then fix.

What to ask the user to check:
1. Both UIs: drives listed; click triangle / double-click opens folders; big folders open fast.
2. Only playable files shown; hidden/system files hidden.
3. Wheel, scroll bar drag, keyboard navigation; selection colours focused vs unfocused; hover.
4. Dark mode toggle live (incl. scroll bar); CUI colours/fonts pages show "Folder Tree".
5. Move the window to a monitor with a different scale: rows re-measure.
6. A drive with no disc / denied folder (e.g. `C:\System Volume Information` with hidden+system
   shown - not possible yet; try an offline network drive) shows "(unavailable)".
