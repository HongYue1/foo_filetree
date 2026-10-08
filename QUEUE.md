# QUEUE - current task only

Interruption-safe handoff. A new thread should be able to read this file, then `AGENTS.md`, and
continue. Update it *while* working (after each finished step), not at the end. When a task is
done, replace this file's contents with the next task from `PLAN.md`.

## Current task: M0 - project skeleton

Goal: `foo_filetree.dll` builds for Release|x64 with no warnings, and loads in foobar2000 showing
an empty panel in Columns UI (Layout tab > Panels > "Folder Tree") and an empty element in Default
UI. Nothing else.

Status: **built, waiting for the user's load test.** M1 may start meanwhile (it does not depend
on the hosts); fix anything the user reports first.

Steps:
- [x] `foo_filetree.vcxproj` from foo_mediabar's (fresh project GUID
      `{DA78E363-8F86-42E7-A5EF-CE6B2C514A3D}`, fb2k-common include dropped, own source list).
- [x] `build.bat` - same pattern as foo_mediabar (SDK libs Release-Static, CUI SDK Release).
- [x] `src/version.h`, `src/component.cpp`, `src/guids.h` (fresh GUIDs: DUI element, CUI panel,
      CUI colour client).
- [x] `src/platform/gdi.h` - `SolidBrush` (cached, recreated only when the colour changes),
      `fill_paint_rect` (fills `rcPaint` only).
- [x] `src/hosts/cui_panel.cpp` - `container_uie_window_v3_t<>`, multi-instance
      `uie::window_factory`, opaque background, colour client (background, text, dark-mode bool),
      `SetWindowTheme(DarkMode_Explorer)` on dark mode.
- [x] `src/hosts/dui_element.cpp` - `ui_element_impl_withpopup`, paints `ui_color_background`,
      follows `ui_element_notify_colors_changed`; config blob passed through untouched.
- [x] Build: 0 warnings, `Build succeeded`; exports only `foobar2000_get_interface`. 257 KB.
- [x] Commit.
- [ ] User test: loads; panel in CUI Layout > Panels; element in DUI (Utility); colours and dark
      mode follow live; two instances work.

Notes / findings so far:
- No `initquit` yet on purpose (2 ms on_init budget, tree built lazily).
- `dumpbin` is not on PATH in Git Bash and `cmd //c "\"...vcvars64.bat\" && dumpbin"` fails on
  quoting. Call it by path: `ls -d "/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/"*/bin/Hostx64/x64`.
