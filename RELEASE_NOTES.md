# Release notes

## 1.1.0

- Favourite files: right-click any file (music, video, a PDF booklet, artwork) > Add to
  favourites, and it shows under one Favourite files root.
- A favourites drop-down in the address bar: right-click a favourite > Add to favourites
  drop-down, then jump to it from the new button next to Up.
- Search every drive and favourite by name (Ctrl+Shift+F, turn it on in Mouse & keys). Results
  fill in as they are found; Esc stops, Esc again brings the tree back.
- The Media Library folders can be roots, and show at once on startup.
- Hide folders: Windows, Program Files and the like are hidden by default, and you can add your
  own (right-click > Hide this folder). Folders with no playable files can be hidden too.
- Drives can be switched off, so only your favourites are roots.
- Read-only mode: browse and play only, nothing in the panel changes files.
- On startup the panel can show the last played track.
- An optional thin scrollbar that only shows while the mouse is over the tree.
- Icons for video, image, text, PDF and playlist files.
- Favourites and Hide this folder work on the whole selection.
- Settings are split into seven tabs: General, Roots, Files, Folders, Look, Mouse & keys, Menu.
- Folders that changed on disk while closed are listed again when opened; adding or removing a
  favourite no longer jumps the tree back to the top.

## 1.0.0

First release. A folder tree panel for foobar2000 v2, in Default UI and Columns UI, 32 and
64-bit.

- Drives and favourite folders as roots; folders are listed in the background, so the panel
  never waits on a slow or offline drive. Natural sort, folders first, by name, date, size or
  type.
- Click, double click, middle click and Enter each run their own action for folders and files:
  play, add, send to a new or named playlist, add to the playback queue, or nothing.
- Multi-selection as in Explorer (Ctrl, Shift, Ctrl+A); playlist actions, copy, cut, delete,
  dragging out and the context menus act on the whole selection.
- Context menu with foobar2000's track menu and the Explorer menu, rename, delete to the Recycle
  Bin with one level of undo, new folder, copy path, properties, open with, save as playlist.
- Drag out to playlists, playlist tabs and Explorer; drop files onto a folder to copy or move.
- Address bar with Back / Forward / Up and typed paths; a filter box that narrows what is open;
  type-ahead find.
- Open folders update when files change on disk. The playing track is marked and can be
  followed.
- Optional icons, tree lines, tooltips, hover highlight, shaded rows and a status bar with counts
  and sizes. Colours, fonts and dark mode follow the host UI.
- Screen readers see the tree (Microsoft Active Accessibility).

Known limits: no per-component font setting (the panel uses the Default UI / Columns UI font);
no total duration in the status bar; copy and move have no panel undo (Explorer's own undo
works).
