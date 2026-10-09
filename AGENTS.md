# AGENTS.md - foo_filetree

A folder tree panel for foobar2000 v2 (Default UI + Columns UI). Display name "Folder Tree".

## Start here

1. Read the workspace `../AGENTS.md` - toolchain, build rules, how to work through the MCP.
2. Read the skills before touching code: `../foobar2000-component-dev/` (always) and
   `../columns-ui-sdk/` (panel, colours, fonts, dark mode). Preferences pages:
   `../foobar2000-component-dev/references/preferences-pages.md`.

## Architecture

Layers, bottom up; everything below `view/` is tested offline (`test/`).

- `platform/` Win32 and fb2k wrappers (DPI, strings, threads, perf counters)
- `fs/` enumeration workers (`FindFirstFileExW` large fetch), change watching
  (`ReadDirectoryChangesW`, open folders only, debounced), playable-type filter
- `model/` node pool, flat visible-row list, natural sort, filter rules, selection, status text
- `view/` the own virtualised tree control (not `SysTreeView32`): GDI double-buffered paint of
  visible rows only, input, rename, drag and drop, address bar, filter box, status bar, MSAA
- `actions/` playlist sends (`process_locations_async`), shell menus and file operations
- `settings/`, `prefs/` global `cfg_var` settings and the Preferences tabs; `hosts/` the CUI
  panel and DUI element (per-panel state: open folders, selection, scroll)

Budget: idle = no timers, no repaints; no disk I/O on the main thread; paint < 1 ms; no
allocations while scrolling; folders listed only when opened.

## Rules

- **Performance first.** Every change keeps these:
  - no disk I/O, shell calls or title-format compilation on the main thread;
  - no timers unless something is actually animating or debouncing;
  - paint only visible rows; invalidate only what changed;
  - no allocations in paint or scroll paths;
  - lazy everything: nothing is enumerated until a folder is expanded or shown.
- **Write findings into the skills.** Anything verified that would have saved time goes into the
  right skill file (`../foobar2000-component-dev/` or `../columns-ui-sdk/`). Verify against the
  SDK headers or a build first; component-specific notes go here.
- **The prior art is reference only.** `foo_uie_explorer_mod` informs *which* features exist.
  Don't copy its code, config layout, names or GUIDs.
- No source file over ~600 lines. If it grows past that, split the module.
- Virtual overrides and window procedures are `noexcept` at the boundary (Columns UI rule).
- Fresh GUIDs only. Never reuse one from `foo_sample`, `foo_mediabar` or the mod.

## Build

```bash
cmd //c build.bat            # Release x64; read build.log, not stdout
grep -iE 'error C|error LNK|warning C|Build FAILED|Build succeeded' build.log
```

Output: `x64/Release/foo_filetree.dll`. Deploy for manual testing (the user copies it):
`%APPDATA%\foobar2000-v2\user-components-x64\foo_filetree\foo_filetree.dll`.
We cannot run foobar2000; never claim something works because it compiled.

## Git

Local repo only. Commit after each finished step with a short, plain message. Never push
or publish unless the user asks. Don't commit build output (see `.gitignore`).

## Voice

README, release notes and commits: plain and direct, first person, no hype.
