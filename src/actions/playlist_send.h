#pragma once

// Sends folders and files to a playlist per an Action. Main thread only. Tag reading, recursion
// and sorting happen in foobar2000's own background operation (process_locations_async); a
// non-recursive folder listing runs on our worker pool first.

#include <windows.h>

#include <string>
#include <vector>

#include "action.h"

namespace filetree::actions {

struct SendItem {
    std::wstring path; //!< "D:\Music\Album" or "D:\Music\Album\01.flac"
    bool is_folder{false};
};

struct SendRequest {
    Action action;
    std::vector<SendItem> items; //!< in this order (one fb2k operation for all of them)
    std::wstring display_name;   //!< name for a new playlist (the folder or file name)
    bool shift{false}; //!< inverts the recursion setting
    bool ctrl{false};  //!< targets the active playlist instead
    HWND parent{};     //!< owner of fb2k's progress dialog
};

void send(const SendRequest& request) noexcept;

//! Writes every playable file in `paths` (folders with their subfolders, fb2k's sort order) to
//! the playlist file `file`; its extension picks the format. Tags are read in fb2k's background
//! operation; failures go to the console.
void save_as_playlist(const std::vector<std::wstring>& paths, std::wstring file,
                      HWND parent) noexcept;

} // namespace filetree::actions
