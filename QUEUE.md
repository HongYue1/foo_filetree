# QUEUE - current task only

Interruption-safe handoff. A new thread should be able to read this file, then `AGENTS.md`, and
continue. Update it *while* working (after each finished step), not at the end. When a task is
done, replace this file's contents with the next task from `PLAN.md`.

## Pending from M0 (fix first if the user reports anything)

M0 (project skeleton, empty CUI panel + DUI element) is built and committed (`1b662d2`). The user
has not yet confirmed it loads. Check: loads; panel in CUI Layout > Panels; element in DUI
(Utility); colours and dark mode follow live; two instances work.

M0 findings:
- No `initquit` yet on purpose (2 ms on_init budget, tree built lazily).
- `dumpbin` is not on PATH in Git Bash and `cmd //c "\"...vcvars64.bat\" && dumpbin"` fails on
  quoting. Call it by path: `ls -d "/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/"*/bin/Hostx64/x64`.

## Done: M1 - model + workers (commit after this edit)

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

Steps:
- [ ] view/layout + view/tree_view skeleton, hooked into both hosts, roots = drives
- [ ] paint (visible rows only), scroll bar, wheel
- [ ] expand/collapse via mouse + async loading + "loading..." / error state
- [ ] keyboard navigation, focus, selection
- [ ] fonts + colours + DPI in both hosts
- [ ] build, commit, hand DLL to user
