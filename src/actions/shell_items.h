#pragma once

// Shell item arrays and data objects for one or several paths (multi-select). The paths may be
// in different folders. Call on a thread with COM initialised (the main thread, or a ComScope
// task on the shell worker).

#include <windows.h>

#include <objidl.h>
#include <shobjidl.h>

#include <string>
#include <vector>

namespace filetree::actions {

//! Fails (and sets *out to null) if any path cannot be parsed or `paths` is empty.
HRESULT make_item_array(const std::vector<std::wstring>& paths, IShellItemArray** out) noexcept;
//! The shell's own data object (CF_HDROP, shell ID lists, drag image support).
HRESULT make_data_object(const std::vector<std::wstring>& paths, IDataObject** out) noexcept;

} // namespace filetree::actions
