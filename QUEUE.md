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

## Current task: M3 - actions

Goal: send folders/files to playlists. PLAN.md was updated (user request 2026-10-08):
configurable single / double / middle click + Enter, each with a folder action and a file action,
any of them "None"; single click always selects and its action (default None) runs in addition,
immediately (no double-click delay timer). Indentation guides / tree lines were added to M5.

Design:
- `actions/action.{h,cpp}` - pure: `Action{kind none/toggle/send, target temp/active/new,
  mode replace/add, play, recursion default/always/never}`, `Bindings` (4 gestures x
  folder/file), defaults, versioned byte encoding (tolerant: bad bytes -> that action's default).
  Covered by the offline tests.
- `actions/action_settings.{h,cpp}` - cfg vars (fresh GUIDs): bindings blob, temp playlist name
  ("Folder Tree"), recursive by default (true). Cached in memory; M5 edits them.
- `actions/playlist_send.{h,cpp}` - Shift inverts recursion, Ctrl targets the active playlist.
  Recursive folder: the folder path goes straight to
  `playlist_incoming_item_filter_v2::process_locations_async` (fb2k recurses, sorts, reads tags
  off the main thread, `op_flag_delay_ui`). Non-recursive folder: our worker lists its playable
  files first. Completion: resolve target, undo backup + clear on replace, add, and for play:
  activate, focus first new item, `playlist_execute_default_action` (honours the user's default
  action; `track_command_settrack` is marked internal in the SDK).
- View: gestures -> binding lookup; expander clicks stay pure toggles. Middle click selects too.

Defaults: single = None/None; double = toggle (folder) / temp replace+play (file); middle = add
to active (both); Enter = same as double.

Steps:
- [ ] action model + encoding + tests
- [ ] settings + send + view wiring
- [ ] build, commit, hand to user
