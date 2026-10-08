# AGENTS.md - foo_filetree

A folder tree panel for foobar2000 v2 (Default UI + Columns UI). Display name "Folder Tree".

## Start here

1. Read `QUEUE.md` - the task in progress and exactly where it stopped. Continue from there.
2. Read `PLAN.md` - goals, performance budget, architecture, the full settings list, milestones.
3. Read the workspace `../AGENTS.md` - toolchain, build rules, how to work through the MCP.
4. Read the skills before touching code: `../foobar2000-component-dev/` (always) and
   `../columns-ui-sdk/` (panel, colours, fonts, dark mode). Preferences pages:
   `../foobar2000-component-dev/references/preferences-pages.md`.

## Rules

- **Keep `QUEUE.md` current.** Update it after every finished step, not at the end: what is done,
  what is next, anything learned that the next thread needs. When a task finishes, replace it
  with the next one from `PLAN.md`. It only ever tracks the current task.
- **Performance first.** Check every change against the budget in `PLAN.md`:
  - no disk I/O, shell calls or title-format compilation on the main thread;
  - no timers unless something is actually animating or debouncing;
  - paint only visible rows; invalidate only what changed;
  - no allocations in paint or scroll paths;
  - lazy everything: nothing is enumerated until a folder is expanded or shown.
- **Write findings into the skills.** Anything verified that would have saved time goes into the
  right skill file (`../foobar2000-component-dev/` or `../columns-ui-sdk/`). Verify against the
  SDK headers or a build first; component-specific notes go here or in `PLAN.md`.
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

Local repo only. Commit after each finished QUEUE step with a short, plain message. Never push
or publish unless the user asks. Don't commit build output (see `.gitignore`).

## Voice

README, release notes and commits: plain and direct, first person, no hype.
