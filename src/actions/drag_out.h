#pragma once

// Dragging a file or folder out of the panel: onto foobar2000 playlists and playlist tabs, into
// Explorer, or any other drop target. Uses the shell's own data object (CF_HDROP, shell ID lists,
// drag image), so every target treats it like a drag from Explorer. Main thread only (OLE).

#include <windows.h>

#include <string>
#include <vector>

namespace filetree::actions {

//! Runs the modal drag loop for `paths` (one or several items) and returns when the item was dropped or the drag was
//! cancelled. Offers copy, move and link with copy preferred: a drop into Explorer moves the
//! user's files only with Shift held, as in Explorer.
//! Returns false if no data object could be made for the paths (nothing was dragged).
bool drag_out(HWND source, const std::vector<std::wstring>& paths) noexcept;

} // namespace filetree::actions
