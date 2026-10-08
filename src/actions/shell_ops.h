#pragma once

// Explorer-style operations on a path. Disk and shell work runs on fs::shell_worker() (COM STA per
// task); completions come back on the main thread. Clipboard work is main-thread only.

#include <windows.h>

#include <functional>
#include <string>
#include <string_view>

namespace filetree::actions {

//! Called on the main thread when a shell operation finished. `changed` is true if the shell
//! may have changed something on disk (refresh then), false if it failed before starting.
using ShellDone = std::function<void(bool changed)>;

//! Opens a folder in Explorer, or opens the file's folder with the file selected.
void open_in_explorer(std::wstring path, bool is_folder) noexcept;

//! Puts `text` on the clipboard as Unicode text. Returns false if the clipboard was busy.
bool copy_text(HWND owner, std::wstring_view text) noexcept;

//! Deletes through the shell (its confirmation, progress and error UI; undo for the recycle
//! bin). `permanent` skips the recycle bin, like Shift+Del in Explorer.
void delete_path(std::wstring path, bool permanent, HWND owner, ShellDone done) noexcept;

//! Renames through the shell (undoable; the shell reports collisions and invalid names).
void rename_path(std::wstring path, std::wstring new_name, HWND owner, ShellDone done) noexcept;

} // namespace filetree::actions
