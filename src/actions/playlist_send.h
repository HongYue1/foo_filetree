#pragma once

// Sends a folder or file to a playlist per an Action. Main thread only. Tag reading, recursion
// and sorting happen in foobar2000's own background operation (process_locations_async); a
// non-recursive folder listing runs on our worker pool first.

#include <windows.h>

#include <string>

#include "action.h"

namespace filetree::actions {

struct SendRequest {
    Action action;
    std::wstring path;         //!< "D:\Music\Album" or "D:\Music\Album\01.flac"
    std::wstring display_name; //!< name for a new playlist (the folder or file name)
    bool is_folder{false};
    bool shift{false}; //!< inverts the recursion setting
    bool ctrl{false};  //!< targets the active playlist instead
    HWND parent{};     //!< owner of fb2k's progress dialog
};

void send(const SendRequest& request) noexcept;

} // namespace filetree::actions
