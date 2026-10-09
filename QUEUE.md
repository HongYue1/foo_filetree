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
- [x] M6d2 follow-ups (user, tested): filter text and cue banner centred (one-line EDIT centred in a frame; same for the typed path); Filter box setting is now In the address bar / Floating over the tree / Off (view/filter_box.* re-parented by Panel; floating opens on Ctrl+F at the tree top right and closes when Esc empties it or it loses focus empty); Ctrl+Backspace deletes the previous word in filter, path and rename boxes (view/edit_util.h). Awaiting user test
- [x] fixes (user): right-click below the rows: Refresh all / Collapse all / Preferences. F5 / Refresh = soft refresh (tree_view_refresh.cpp): every open folder re-listed in the background, Tree::children_match skips unchanged ones, Tree::merge_children swaps in changes in one splice keeping open subtrees, selection and top row (moved child indices remapped; their pending listings re-requested). Expanding an already-listed folder also re-checks it. User-tested OK. Rename/delete/undo in an open folder now merge too (reload_and_select → request_check + pending select), fixing the jump when deleting under D:\ while deep inside it. User-tested OK
- [x] M6e1: per-instance state + startup modes. settings/panel_state.* (UTF-8 text blob "foo_filetree state 1", E/S/T lines; tested), TreeView::capture_state/restore_state (+ restore_top_ held until restore finishes or the user scrolls), Panel::start by setting Startup {restore, collapsed, folder} + startup_folder (General tab, folder picker). DUI: get/set_configuration from the live tree; CUI: set_config/get_config (live when the window exists), captured in WM_DESTROY. User-tested OK
- [x] M6e2: favourites as roots. Settings favourites (vector, cfg_string '|'-joined; clean_path/split_paths tested) + favourites_place before/after drives (change_roots → relist_all). node_favourite root flag, model::display_name (last component). Context menu item MenuItem::favourite (Add to / Remove from favourites, folders only; default place after Copy path; decode inserts new items by default rank). Preferences Favourites tab (list, Add... folder picker, Remove, Move up/down, placement). User-tested OK
- [x] M6f (user): optional icons + favourite star. view/icon_font.* (Segoe Fluent Icons, else Segoe MDL2 Assets; glyphs drawn as text in the theme colour: folder/open folder/drive/document/audio for playable). Settings show_icons (off, change_remeasure) and mark_favourites (on, change_repaint) on the Display tab. Metrics icon/icon_width, content_left; star after the name of any folder in the favourites (is_favourite: leaf-name set first, full path only on a hit). User-tested OK
- [x] M6g (user): optional separator line between favourites and drives (separate_favourites, Favourites tab; + favourites_gap 0-24 DIP, default 12 (user): TreeView::boundary_row (cached, self-validating), gap_above/row_top; row_at, invalidate_row, paint loop, scroll_to (full repaint when the gap scrolls in/out), rename edit, menu point use them; visible_rows subtracts the gap; line drawn mid-gap). User-tested OK

## Current task: M7 - watching, file operations, now playing

- [x] M7a: change watching. fs/watcher.* (ReadDirectoryChangesW, one IOCP thread, open on the thread, unwatch via closing flag + CancelIoEx; tested: notify, unwatch, missing, deleted folder, shutdown). view/tree_view_watch.cpp: watch set = open listed folders on the rows (max 256, no removable/optical drives), synced 100 ms after row changes; notifications batched 250 ms then request_check (soft refresh merge). Setting watch_changes (General, default on). Shutdown in on_quit. User-tested OK. Fix (user): deleting/restoring a watched subfolder needed two refreshes: a notification while a check/listing was in flight was dropped (dedup), and that result could predate the change (a deleted watched folder stays listed until our handle closes). PendingListing::again now re-checks once after the in-flight result lands. Still two refreshes (also with watching off): the real cause was merge_listing dropping in-flight checks of moved children (the parent's check lands first because the child folder's time changed, which moves the child to a new index); moved checks are now re-requested under the new index like listings. User-tested OK (again kept: it covers a change landing while a listing is in flight)
- [x] M7b: actions/file_ops.* (new_folder: first free "New folder (n)" + IFileOperation::NewItem; copy_items: IFileOperation Copy/MoveItems, "- Copy" names when copying into the same folder, move into the same folder skipped; clipboard: shell data object + Preferred DropEffect, OleFlushClipboard; read CF_HDROP; pasted cut empties the clipboard if unchanged), actions/shell_common.h (ComScope, top_level, finish shared with shell_ops). view/drop_target.* (OLE IDropTarget + IDropTargetHelper drag image), view/tree_view_fileops.cpp: F7 / menu New folder (opens the folder, selects and renames the new one: PendingSelect::rename), Ctrl+C/X/V (Ctrl+Shift+C now copies the path, as Explorer), menu Paste (only when files are on the clipboard), drop onto a folder row (a file row means its folder): highlighted like hover, copy, Shift move (we move on a worker and report an optimized move so the source deletes nothing), 800 ms hover opens a closed folder, edge auto-scroll. Drag out now offers move too with copy preferred (Shift+drag into Explorer moves). MenuItem new_folder, paste (14 items; tests). Main-menu commands moved to M7c. User test: all OK except: (1) dropping showed the no-drop cursor: RegisterDragDrop was never called (a scripted edit missed silently; skill note added); (2) the New folder rename box closed after a second: a background merge/splice (the parent's check, the folder's time changed) ended the rename; now merges and splices move the editor along (follow_rename, edit_node_ remapped) and only cancel it when the item is gone or out of view. Added (user): Cut / Copy menu items (16 items), the cut item drawn dimmed (text + icon) until the clipboard changes (WM_CLIPBOARDUPDATE listener only while a cut is shown; remapped on merge). User-tested OK
- [x] M7c1: view/now_playing.* (play_callback_static new_track/stop, file:// paths only, asks get_now_playing once for panels made during playback; listeners), tree_view_playing.cpp: resolve_playing walks each root holding the path through open listed folders only (ends on a visible row; the file beats folders; up to 4 marks), after every splice/merge/populate; paint: marks after the name (star + speaker E767 / fallback triangle; the file in text colour, a closed folder holding it dimmer). Setting mark_playing (Display tab, default on; sorting section moved down 8 DLU). view/main_menu.cpp: View > Folder Tree popup (Show now playing = navigate_to, Refresh, Collapse all, New folder) on TreeView::active() (last focused panel, registry in tree_view.cpp); disabled without a panel / nothing playing. dialog_check 0, 225 checks. User: speaker looked bad: now PlaySolid F5B0 (in MDL2 and Fluent), and with icons on the playing file's icon itself becomes the triangle (text colour) instead of a trailing mark. Send to new playlist now also activates the new playlist (user). User: OK, but the trailing mark was too big: play mark after a name now drawn with mark_font_ (10 px at 100%; the star too, user). "Show icons" renamed "File and folder icons". Playback queue (user agreed): Target::queue (queue_add_item per item; folders recursive like other sends), preset "Add to playback queue" (click/middle/Enter), MenuItem::queue after Send to new playlist (17 items; tests). User-tested OK (star made smaller too)
- [x] fix (user): play mark sat low (DT_VCENTER centres the font cell, icon glyphs sit anywhere in it): measure_marks places the star and play marks by their ink (GetGlyphOutline GGO_METRICS) centred on the text's capitals (otmsCapEmHeight). Sorting row compacted. User: only right with 2 px raise, and wanted size not offset: capitals now measured on the ink of "H" (otmsCapEmHeight was off); raise settings replaced by icon_size (12-24 DIP, default 16) and mark_size (8-16, default 10), Display tab "Icon size" / "Star & play size" (mark default 12, user). User-tested OK
- [x] defaults (user): filter box floating, connector lines, file and folder icons on (the rest already matched the user's settings). Filter tab hint without the Synology note.
- [x] M7c2: shell_ops show_properties (SHObjectProperties), open_with (SHOpenWithDialog), pick_playlist_file (IFileSaveDialog m3u8/fpl/m3u); playlist_send save_as_playlist (recursive process_locations_async -> playlist_loader::g_save_playlist, console messages). MenuItem save_playlist (folders), open_with (files), properties (20 items; tests). Alt+Enter = Properties. Setting follow_playing (Display tab "Select each new track", default off): navigates on each new track unless renaming. Add to queue was done in M7c1. Library indicators dropped (user). User test: Properties, Open with, Save as playlist OK; Select each new track did nothing: settings diff() ignored follow_playing so Apply never reached the panels (fixed + test). 226 checks, dialog_check 0
- [x] M7c2 follow-up: user-tested OK (Select each new track stays off by default, user)
- [ ] Footer status / performance counters from the M7 list: not started; optional, ask user whether wanted

## Next task: M7d - multi-select (user asked, 2026-10-09; see PLAN.md M7d)

- [x] M7d1 (built, awaiting user test): node_selected flag + Tree::selected_ list + anchor_ (model/tree_selection.cpp; merge moves flag and anchor, collapse/collapse_all deselect hidden descendants, filter keeps hidden rows selected; test/selection_test.cpp, 244 checks). view/tree_view_select.cpp: select_row (single), focus_row, toggle_row, extend_to, move_to, select_all, selection_or_focus. Keys: Shift+nav extends, Ctrl+nav moves focus, Ctrl+Space, Ctrl+A. Mouse: Ctrl/Shift click (no action), a press on a multi-selected row keeps the selection until release (drag). Paint: flag = selected fill; focus frame (DC brush) when several are selected or the focus row is unselected. Focus inside a collapsed folder: the folder is selected if nothing else is. Original spec: per-node selected flag (or a set of node ids remapped on merge/splice like edit_node_), anchor + focus row (focus = today's selected_row_). Click = single; Ctrl+click toggle (no action binding fires); Shift+click / Shift+arrows/Home/End/PgUp/PgDn range from anchor; Ctrl+arrows move focus only, Ctrl+Space toggles; Ctrl+A all visible rows. Paint: selected fill for all, focus rect on focus row. Keep through soft refresh, filter changes, collapse (collapsed children drop out). Tests for range/toggle on the pure model.
- [x] M7d2 (built, awaiting user test): view/tree_view_items.cpp: actions_for(node) = the selection when node is selected or focused, else node; send (SendRequest::items, one process_locations_async; non-recursive folders gathered by actions/playlist_send.cpp Gather), drag, Copy/Cut (set_clipboard_files, cut_nodes_ sorted + remapped on merge), Copy path (CRLF), Delete (delete_paths: one IFileOperation::DeleteItems; undo only for one item; other source folders re-checked), Properties (SHMultiFileProperties for several), Save as playlist (any items), fb2k submenu (files among the items), Explorer submenu (several only if same_parent). actions/shell_items.* make_item_array/make_data_object. Menu greys Rename/New folder/Open with/favourite for several; right-click inside the selection keeps it. Moved delete/reload_and_select/apply_pending_select out of tree_view_menu.cpp. Behaviour change: Shift/Ctrl+click no longer run the click action (README: modifiers apply to Enter/middle click). Original spec: play / add / new playlist / queue / save playlist (one process_locations_async with all paths), Copy/Cut/Ctrl+Shift+C (paths joined by CRLF), Delete (count in the confirmation), drag out (shell item array of all), Explorer submenu (IContextMenu on several items of one parent; else only the focus item), fb2k context menu on all tracks. Single only: Rename, New folder, Open with, Properties (or SHMultiFileProperties for several). Right-click on an unselected row selects it alone first (Explorer).
- [x] tree_view.cpp split (was ~800 lines): layout/scrolling moved to view/tree_view_layout.cpp; every source file is now under 600 lines.
- [~] M7d3: README keys table + multi-select paragraph done; awaiting user test of M7d1+M7d2; then M8 (PLAN.md)
