#pragma once

// Explorer-style operations on a path. Disk and shell work runs on fs::shell_worker() (COM STA per
// task); completions come back on the main thread. Clipboard work is main-thread only.

#include <windows.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

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
//! bin). `permanent` skips the recycle bin, like Shift+Del in Explorer. Several paths go in one
//! operation (one confirmation with the count, one progress dialog).
void delete_paths(std::vector<std::wstring> paths, bool permanent, HWND owner,
                  ShellDone done) noexcept;

//! Renames through the shell (undoable; the shell reports collisions and invalid names).
void rename_path(std::wstring path, std::wstring new_name, HWND owner, ShellDone done) noexcept;

//! The shell's Properties sheet for the paths (modeless; Alt+Enter in Explorer). Several paths
//! get the combined sheet (SHMultiFileProperties).
void show_properties(const std::vector<std::wstring>& paths, HWND owner) noexcept;

//! Windows' "Open with" chooser for a file; opens it with the chosen program.
void open_with(const std::wstring& path, HWND owner) noexcept;

//! Save dialog for a playlist: starts in `folder` with `name`; offers m3u8, fpl and m3u.
//! Returns false if cancelled.
bool pick_playlist_file(HWND owner, const std::wstring& folder, const std::wstring& name,
                        std::wstring& out) noexcept;

//! Puts back the most recently recycled item that was deleted from `path` (Undo delete).
//! See recycle_bin.h.
//! Several paths: each is restored; succeeded only if all were, ran if any was.
void restore_recycled(std::vector<std::wstring> paths, ShellDone done) noexcept;

} // namespace filetree::actions
