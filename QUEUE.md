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

## Done: M2 - tree view (user-tested 2026-10-08)

Virtualised view in `view/`, both hosts. User confirmed: drives, expand/collapse (mouse, Enter,
arrows), filtering, natural sort, scrolling, keys, colours, hover, dark mode, CUI colour/font
pages, two instances. PgUp/PgDn = parent folder / past the parent's subtree (user's choice;
`9a7a4cb`, confirmed). "(unavailable)" state untested (no offline drive). Enter needed
`DLGC_WANTMESSAGE` (recorded in foobar2000-component-dev/references/sdk-quirks.md).

## Done: M3 - actions (user-tested 2026-10-08, `968628e`)

Click/Enter bindings (folder + file action per gesture, None allowed), send via
`process_locations_async`, Shift = invert recursion, Ctrl = active playlist. All four user
checks passed; 3511-file folder: only fb2k's own progress dialog, UI never froze. Double-click
on a folder keeps toggling (user chose "keep as is"). Leftover: archives in the playable set.
Finding: `pfc_infinite` is `int` -> C4245; use `SIZE_MAX` (in sdk-quirks.md).

## Done: M4 - context menu (`d7bd810`, `571a576`, `af92e29`, user-tested)

Own items + lazy foobar2000 / Explorer submenus, inline rename, Recycle Bin delete, refresh,
copy path, one-level panel undo (Ctrl+Z; Recycle Bin restore via the `$I` index). README.md
documents keys, menu, Shift for Explorer's extra verbs, undo. Apps key untested (no key).

## Current task: M5 - settings + Preferences

Scope (what exists now; features from M6+ get their settings with them): General, Display,
Filter, Actions, Menu tabs. Deferred: icons (M8), address bar/startup/favourites (M6),
"don't send to playlist" list (needs our own recursion; revisit with M7), tooltips (M8).

Settings:
- General: hidden drives (checklist of detected drives, bitmask A-Z).
- Display: tree lines none / connectors / guides, thickness 1-4 DIP, colour = text at N%
  opacity (default 35) or custom (+ CUI colour client entry "Tree lines"); file extensions
  always / never / only for non-playable files; row padding 0-12 DIP (default 3); sort field,
  folders first, reverse.
- Filter: show hidden, show system, files all / playable / none, always-show extensions,
  never-show extensions, hide patterns (globs on names, files and folders, e.g. `@eaDir`).
- Actions: binding editor (4 gestures x folder/file: none / expand-collapse / send with target,
  mode, play, recursion), recursive by default, temp playlist name.
- Menu: which items appear, their order (separators auto between groups).

Design:
- Pure, tested: `model/filter_rules.*` (glob match, include/exclude sets, hide patterns, shared
  read-only with workers via EnumOptions), `settings/settings_model.*` (structs, sanitize,
  diff -> change mask: repaint / remeasure / relist / roots, menu layout encode/decode).
- `settings/settings_store.cpp`: one cfg var per setting (fresh GUIDs), `current()`,
  `apply(new)`: save, diff, notify live views (registry in view). Relist = reload every loaded
  folder keeping expansion + selection by path (M5a: reload expanded roots; good enough).
- Preferences: one page "Folder Tree" under Tools (guid_tools, like the user's other components); tabs as child dialogs (skill
  preferences-pages.md: 300x246 DU, child per tab, dark hooks per child, guarded WM_NOTIFY,
  style profile comment in the .rc). `dialog_check.bat` zero problems after every layout change.

Steps:
- [x] M5a: filter_rules + settings_model + tests (153 checks; test/check.h shared, new
  test/settings_test.cpp); settings_store (18 cfg vars); view applies settings
  (tree_view_settings.cpp: relist_all restores expanded paths + selection; hidden drives; row
  padding; extension display; tree lines in paint_lines). Not user-visible until M5b.
- [x] M5b: Preferences page (src/prefs/preferences.cpp, prefs_util.*, foo_filetree.rc,
  resource.h; Tools > Folder Tree) with General / Display / Filter tabs. dialog_check 0
  problems. Drive check boxes are created at run time (before the dark hooks).
- [x] M5b screenshots light + dark look right. User asked: live preview -> settings::preview()
  / end_preview() (current() = preview, stored() = saved), page previews 250 ms after an edit,
  WM_DESTROY without Apply reverts. Pattern hint now explains * ? ; and @eaDir (Synology).
- [x] user asked: drag & drop out (to playlists, playlist tabs, Explorer) -> moved from M7 to
  the start of M6 (PLAN.md updated).
- [x] M5b user test: Apply effects live (lines, padding,
  extensions, sort, filter, hidden drives, patterns); Reset
- [x] M5c: Actions tab (binding editor) + Menu tab; menu honours layout
- [x] M5d: dropped. CUI group_foreground made Columns UI bug-check (crash) on scheme change and was black in the dark Global scheme; reverted, lines use text colour at N% opacity in both UIs

## Current task: M6 - navigation and drag out

- [x] M6a: drag out (actions/drag_out.*): DragDetect in on_button_down, shell data object via
  SHCreateShellItemArrayFromIDLists + BHID_DataObject, SHDoDragDrop, copy/link only (never move).
  User-tested OK (playlist, playlist tabs CUI+DUI, Explorer, Esc cancels, click and
  double click still work).
- [x] M6b: type-ahead find (on_char; 1 s reset like Explorer; repeated letter cycles; case-insensitive prefix on visible rows). User-tested OK
- [ ] M6c: moved to M7 after file operations (F5 copy, F6 move, F7 new folder need them first)
- [x] M6d: address bar (view/panel.* hosts address bar + tree as child windows; view/address_bar.*: Back/Forward/Up, crumbs, click blank or Ctrl+L to type a path; tree_view_nav.cpp navigate_to via restore sets; history keeps selections held >= 0.8 s; Alt+Left/Right/Up, mouse side buttons; General tab "Show the address bar"). User-tested OK (CUI+DUI, dark)
- [~] M6d2: filter box (right of the address bar; Tree::set_filter rebuilds rows: name contains text, or whole-name * ? glob; matching folders keep contents, ancestors shown; RowSplice::full + previous_node_at keeps selection/top; Ctrl+F, Esc clears, address navigation clears; General tab "Show the filter box"). 179 checks, dialog_check 0; awaiting user test
- [~] fix: hover highlight stayed on the old row after a wheel scroll (ScrollWindowEx moved it); now the hovered row is repainted plain before the scroll and the row under the mouse is hovered after. Awaiting user test
- [ ] M6e: favourites as roots, startup modes, per-instance state
