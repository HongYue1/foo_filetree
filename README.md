# Folder Tree (foo_filetree)

A fast folder tree panel for foobar2000 v2. Works in Default UI and Columns UI.

## Install

Preferences > Components > Install..., pick `foo_filetree.fb2k-component`, restart foobar2000.
The package holds a 32-bit and a 64-bit DLL; foobar2000 for ARM uses the 64-bit one. Then add
the panel: Default UI layout editing > Utility > Folder Tree, or Columns UI Layout > Panels >
Folder Tree.

## Mouse and keys

| Input | Action |
| --- | --- |
| Click | Select (one item) |
| Ctrl+click / Ctrl+Space | Add the item to the selection or take it out |
| Shift+click / Shift+arrows, Home, End, Page Up / Down | Select a range (Ctrl+Shift+click adds it) |
| Ctrl+arrows | Move the focus without changing the selection |
| Ctrl+A | Select every visible row |
| Double click / Enter | Folder: expand or collapse. File: play it in the "Folder Tree" playlist |
| Middle click | Add to the active playlist |
| Shift + Enter / middle click | Flip "include subfolders" for that action |
| Ctrl + Enter / middle click | Send to the active playlist instead |
| Arrows, Home, End, + / - | Move, expand, collapse |
| Page Up / Page Down | Parent folder / next folder after the parent |
| F2 | Rename (Enter saves, Esc or clicking away cancels) |
| Del / Shift+Del | Delete to the Recycle Bin / delete permanently |
| F5 | Refresh: every open folder is checked again; changes appear in place |
| F7 | New folder (inside the selected folder, or next to the selected file), then rename it |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut the selected items / paste files into the focused folder (as in Explorer) |
| Ctrl+Shift+C | Copy the full paths (one per line) |
| Alt+Enter | Properties (the Explorer properties dialog) |
| Ctrl+Z | Undo the last rename or delete (a delete of several items is one undo) |
| Right click, Apps key, Shift+F10 | Context menu |

- With several items selected, Enter, Play, the playlist commands, Add to playback queue, Save as
  playlist, Copy, Cut, Copy path, Delete, Properties, dragging out and the foobar2000 submenu act
  on all of them (one playlist operation; one delete confirmation). The Explorer submenu covers
  them when they are in one folder, else only the clicked item. Rename, New folder, Open with and
  favourites stay single-item. Right-clicking inside the selection keeps it; pressing on a
  selected item and dragging drags them all.
- Drop files from Explorer onto a folder to copy them there, hold Shift to move. Hovering over a
  closed folder opens it; near the top or bottom edge the tree scrolls. Dragging out of the panel
  copies too, unless Shift is held.
- The playing track is marked with a play triangle (its icon, or after the name without icons);
  while its folder is closed, the closed folder holding it gets a dimmer one (Display tab: "Mark the playing track").
  "Select each new track" moves the selection to every new track as it starts.
- Context menu extras: Open with (files), Properties, Save as playlist (folders: all their tracks,
  recursively, into an .m3u8 / .fpl / .m3u file).
- Main menu **View > Folder Tree**: Show now playing, Refresh, Collapse all, New folder. They act
  on the panel focused last and can be given keyboard shortcuts in Preferences > Keyboard Shortcuts.
- Type letters to jump to the next visible row whose name starts with them; press the same letter
  again to step through matches. A pause of one second starts a new search.

## Address bar

Shows the selected item's path; click a part to go there. Click the empty part or press Ctrl+L
to type a path (environment variables like %USERPROFILE% work), Enter goes there, Esc cancels.
Back, Forward and Up are also Alt+Left, Alt+Right, Alt+Up and the mouse side buttons. Places you
stay on for a moment are remembered; quick arrow-key moves are not. Hide it on the General tab.

## Filter box

On the right of the address bar, or floating over the tree (General tab; Ctrl+F opens it). Typing narrows the open folders to names containing the
text, or matching it with * and ? (`*.flac`). Folders above a match stay, a matching folder keeps
its contents. Esc clears it; Enter or Down goes back to the tree. It filters what is open, it does
not search the disk.

## Context menu

- **Play**, **Add to active playlist**, **Send to new playlist**
- **Open in Explorer** (files: **Show in folder**), **Copy path**
- **Rename**, **Delete**, **Refresh**
- **foobar2000 >** (files only): foobar2000's usual track menu
- **Explorer >**: the Windows right-click menu (Open with, Send to, Properties, ...)

**Tip:** hold **Shift** while right-clicking to get Explorer's extra commands in
**Explorer >** (for example "Copy as path" or "Open in Terminal"), just as in Explorer itself.

## Undoing a delete or rename

Press **Ctrl+Z** in the panel (or use **Undo ...** in the context menu) to undo the last rename
or delete made in Folder Tree. One step only. A deleted item comes back from the Recycle Bin;
Shift+Del deletes permanently and cannot be undone. The same changes can also be undone with
Ctrl+Z in an Explorer window, or restored from the Recycle Bin by hand.

Open folders update by themselves when files change on disk (General > Update open folders...);
removable and optical drives are not watched, so they do not block Safely Remove: press F5 there.

## Preferences

Settings live under Preferences > Tools > Folder Tree. Changes preview live in open panels and are
kept only when you press OK or Apply.

Each panel remembers its own open folders, selection and scroll position (stored with the
foobar2000 layout). General > On startup chooses between restoring that, starting with all
folders closed, or opening a fixed folder.

The View tab has the tooltips (off, full path, or only cut-off names shown in place), the hover
highlight, shading every other row and the status bar. The status bar shows how many items are
selected with their size, or what the focused folder holds (folders, files, size of the files);
rest the mouse on it for the panel's counters (rows, nodes, memory, watched folders, listings in
flight).

Favourites: right-click a folder > Add to favourites and it becomes a root (shown by its name)
in every panel, before or after the drives. Manage the list on the Favourites tab. Folders that are
favourites get a star (Display > Star favourite folders). Display > Show icons adds folder, file
and drive icons drawn from the Windows icon font, in the theme's colours.


## Accessibility

The tree answers screen readers through Microsoft Active Accessibility: it is an outline whose
items have their name, level, selected / expanded state and position, and focus and selection
changes are announced. Narrator reaches it through Windows' UI Automation bridge; NVDA and JAWS
read it directly. The address bar and filter box are standard edit controls.

## Building

Visual Studio 2026 with the C++ desktop workload and ATL, WTL 10 next to the project
(`..\wtl`), the foobar2000 SDK 2026-09-17 with the Columns UI SDK in `..\SDK-2026-09-17`.
`build.bat [Release|Debug] [x64|Win32]` builds; `test\build_tests.bat` runs the offline tests;
`package.bat` builds both platforms into `dist\foo_filetree.fb2k-component` (PDBs in
`dist\symbols`).
