#pragma once

// Dragging a file or folder out of the panel: onto foobar2000 playlists and playlist tabs, into
// Explorer, or any other drop target. Uses the shell's own data object (CF_HDROP, shell ID lists,
// drag image), so every target treats it like a drag from Explorer. Main thread only (OLE).

#include <windows.h>

#include <string>

namespace filetree::actions {

//! Runs the modal drag loop for `path` and returns when the item was dropped or the drag was
//! cancelled. Offers copy and link only: a drop into Explorer never moves the user's files.
//! Returns false if no data object could be made for the path (nothing was dragged).
bool drag_out(HWND source, const std::wstring& path) noexcept;

} // namespace filetree::actions
