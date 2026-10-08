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

## Current task: M4 - context menu

Goal: right-click menu with our items, the fb2k context menu (files), the Shell menu, rename,
delete, copy path, open in Explorer, refresh. Item choice/order is a setting in M5.

Design:
- Menu: Play / Add to active playlist / Send to new playlist | Open in Explorer (file: show in
  folder) / Copy path | Rename (F2) / Delete (Del) / Refresh (F5) | foobar2000 > (files) /
  Shell >. Right-click selects the row first; Apps key / Shift+F10 opens at the selection.
  DUI layout-edit mode still gets Default UI's menu.
- Disk-touching shell work (open in Explorer, recycle, rename) runs on a worker with its own
  STA COM init (`IFileOperation`, undoable, shell conflict UI). `EnumerationService::submit()`
  exposes the pool for that. Copy path is main thread (clipboard only).
- Shell submenu: built on demand on the main thread (STA), `SHParseDisplayName` +
  `SHBindToParent` + `IContextMenu`(2/3), menu messages forwarded while it is open, never cached.
- fb2k submenu: `contextmenu_manager` over a handle made from the file path (no tag read).
- Rename: inline EDIT over the row (dark-themed, host font), Enter commits, Esc / focus loss /
  scroll cancels.
- After rename/delete/refresh: `Tree::reload(node)` drops the folder's rows, marks it unloaded
  and re-lists it; the view re-selects by name (renamed item, or the next sibling after delete).
  Old children stay orphaned in the pool until M7 compaction.

Steps:
- [ ] Tree::reload + tests; EnumerationService::submit
- [ ] shell ops, shell menu, fb2k menu
- [ ] view: context menu, shortcuts, inline rename, reselect
- [ ] build, commit, hand to user
