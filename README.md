# Folder Tree (foo_filetree)

A fast folder tree panel for foobar2000 v2. Works in Default UI and Columns UI.

## Mouse and keys

| Input | Action |
| --- | --- |
| Click | Select |
| Double click / Enter | Folder: expand or collapse. File: play it in the "Folder Tree" playlist |
| Middle click | Add to the active playlist |
| Shift + click action | Flip "include subfolders" for that action |
| Ctrl + click action | Send to the active playlist instead |
| Arrows, Home, End, + / - | Move, expand, collapse |
| Page Up / Page Down | Parent folder / next folder after the parent |
| F2 | Rename (Enter saves, Esc or clicking away cancels) |
| Del / Shift+Del | Delete to the Recycle Bin / delete permanently |
| F5 | Refresh: every open folder is checked again; changes appear in place |
| Ctrl+C | Copy the full path |
| Ctrl+Z | Undo the last rename or delete |
| Right click, Apps key, Shift+F10 | Context menu |

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

Changes made through **Explorer >** are not picked up automatically yet; press F5.

## Preferences

Settings live under Preferences > Tools > Folder Tree. Changes preview live in open panels and are
kept only when you press OK or Apply.

Each panel remembers its own open folders, selection and scroll position (stored with the
foobar2000 layout). General > On startup chooses between restoring that, starting with all
folders closed, or opening a fixed folder.

Favourites: right-click a folder > Add to favourites and it becomes a root (shown by its name)
in every panel, before or after the drives. Manage the list on the Favourites tab. Folders that are
favourites get a star (Display > Star favourite folders). Display > Show icons adds folder, file
and drive icons drawn from the Windows icon font, in the theme's colours.

