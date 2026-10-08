# QUEUE - current task only

Interruption-safe handoff. A new thread should be able to read this file, then `AGENTS.md`, and
continue. Update it *while* working (after each finished step), not at the end. When a task is
done, replace this file's contents with the next task from `PLAN.md`.

## Current task: M0 - project skeleton

Goal: `foo_filetree.dll` builds for Release|x64 with no warnings, and loads in foobar2000 showing
an empty panel in Columns UI (Layout tab > Panels > "Folder Tree") and an empty element in Default
UI. Nothing else.

Status: **not started** (PLAN.md, QUEUE.md, AGENTS.md and the git repo were created in the
previous step).

Steps:
- [ ] Copy project settings from `../foo_mediabar/foo_mediabar.vcxproj` (it already has the
      correct relative SDK paths, `/MT`, `/d2notypeopt`, C++23, warning level, project
      references incl. `columns_ui-sdk-public`). Rename to `foo_filetree.vcxproj`, fresh project
      GUID, strip mediabar's source list.
- [ ] `build.bat` - same pattern as `../foo_mediabar/build.bat` (SDK libs as Release-Static,
      CUI SDK as Release, log to `build.log`).
- [ ] `src/version.h` (`FILETREE_NAME "Folder Tree"`, `FILETREE_VERSION "0.1.0"`),
      `src/component.cpp` (`DECLARE_COMPONENT_VERSION`, `VALIDATE_COMPONENT_FILENAME`),
      `src/guids.h` with fresh GUIDs (generate with `powershell [guid]::NewGuid()`).
- [ ] `src/hosts/cui_panel.cpp` - `uie::container_uie_window_v3_t<>`, multi-instance factory,
      paints CUI background colour; colours client with dark-mode bool.
- [ ] `src/hosts/dui_element.cpp` - `ui_element` + `ui_element_instance`, paints DUI background.
- [ ] Build, grep `build.log` for `error|warning|Build succeeded`, check exports with
      `dumpbin -exports`.
- [ ] Commit. Tell the user the DLL path and ask them to load it.

Notes / findings so far:
- (none yet)
