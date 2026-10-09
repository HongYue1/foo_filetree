#pragma once

// New folder, copy / move into a folder, and the shell clipboard (Ctrl+X/C/V). Disk work runs on
// fs::shell_worker() through IFileOperation (the shell's progress, conflict and error UI, undo in
// Explorer); completions come back on the main thread. Clipboard and IDataObject reading are
// main-thread only.

#include <windows.h>

#include <objidl.h>

#include <functional>
#include <string>
#include <vector>

#include "shell_ops.h"

namespace filetree::actions {

//! Called on the main thread with the created folder's name (empty if nothing was created).
using NewFolderDone = std::function<void(ShellResult, std::wstring)>;

//! Creates "New folder" (or "New folder (2)", ...) in `parent`.
void new_folder(std::wstring parent, HWND owner, NewFolderDone done) noexcept;

//! Copies or moves `paths` into `folder`. A copy into the items' own folder gets Explorer's
//! "- Copy" names instead of the replace dialog; a move into it does nothing (ran = false).
void copy_items(std::vector<std::wstring> paths, std::wstring folder, bool move, HWND owner,
                ShellDone done) noexcept;

//! Puts `paths` on the clipboard as files, like Ctrl+C / Ctrl+X in Explorer (any program that
//! pastes files accepts it). Returns false if that failed.
bool set_clipboard_files(const std::vector<std::wstring>& paths, bool cut) noexcept;

//! Files on the clipboard (CF_HDROP) and whether they were cut.
struct ClipboardFiles {
    std::vector<std::wstring> paths;
    bool cut{false};
    DWORD sequence{0}; //!< GetClipboardSequenceNumber() when read
};

//! Cheap: are there files on the clipboard?
[[nodiscard]] bool clipboard_has_files() noexcept;
bool read_clipboard_files(HWND owner, ClipboardFiles& out) noexcept;
//! After a cut was pasted: empties the clipboard if it still holds what was read (as Explorer).
void clear_clipboard_if(HWND owner, DWORD sequence) noexcept;

//! Does a drag carry files (CF_HDROP)?
[[nodiscard]] bool has_files(IDataObject* data) noexcept;
bool read_files(IDataObject* data, std::vector<std::wstring>& out) noexcept;
//! Tells the drag source that the target moved the files itself (an optimized move), so it must
//! not delete anything.
void report_optimized_move(IDataObject* data) noexcept;

} // namespace filetree::actions
