#pragma once

// Explorer-style operations on a path. Disk and shell work runs on fs::shell_worker() (COM STA per
// task); completions come back on the main thread. Clipboard work is main-thread only.

#include <windows.h>

#include <functional>
#include <string>
#include <string_view>

namespace filetree::actions {

struct ShellResult {
    bool ran{false};       //!< the operation started: something on disk may have changed
    bool succeeded{false}; //!< it completed, nothing failed or was cancelled
};

//! Called on the main thread when a shell operation finished.
using ShellDone = std::function<void(ShellResult)>;

//! Opens a folder in Explorer, or opens the file's folder with the file selected.
void open_in_explorer(std::wstring path, bool is_folder) noexcept;

//! Puts `text` on the clipboard as Unicode text. Returns false if the clipboard was busy.
bool copy_text(HWND owner, std::wstring_view text) noexcept;

//! Deletes through the shell (its confirmation, progress and error UI; undo for the recycle
//! bin). `permanent` skips the recycle bin, like Shift+Del in Explorer.
void delete_path(std::wstring path, bool permanent, HWND owner, ShellDone done) noexcept;

//! Renames through the shell (undoable; the shell reports collisions and invalid names).
void rename_path(std::wstring path, std::wstring new_name, HWND owner, ShellDone done) noexcept;

//! Puts back the most recently recycled item that was deleted from `path` (Undo delete).
//! See recycle_bin.h.
void restore_recycled(std::wstring path, ShellDone done) noexcept;

} // namespace filetree::actions
