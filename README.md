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
| F5 | Refresh the folder (a file refreshes its folder) |
| Ctrl+C | Copy the full path |
| Right click, Apps key, Shift+F10 | Context menu |

## Context menu

- **Play**, **Add to active playlist**, **Send to new playlist**
- **Open in Explorer** (files: **Show in folder**), **Copy path**
- **Rename**, **Delete**, **Refresh**
- **foobar2000 >** (files only): foobar2000's usual track menu
- **Explorer >**: the Windows right-click menu (Open with, Send to, Properties, ...)

**Tip:** hold **Shift** while right-clicking to get Explorer's extra commands in
**Explorer >** (for example "Copy as path" or "Open in Terminal"), just as in Explorer itself.

## Undoing a delete or rename

Deletes go to the Recycle Bin unless you use Shift+Del. To get a file back, open the Recycle Bin
and choose **Restore**. Renames and Recycle Bin deletes are also recorded in Windows' shared undo
history: open any Explorer window and press **Ctrl+Z** (only straight after the change; later
Explorer actions are undone first).

Changes made through **Explorer >** are not picked up automatically yet; press F5.
