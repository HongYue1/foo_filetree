#pragma once

// Drive roots. GetLogicalDrives() reads a bitmask from the kernel and touches no disk, so this is
// safe on the main thread. Drive types and volume labels can block (network, optical) and are
// fetched on a worker when the view needs them (M2).

#include <string>
#include <vector>

namespace filetree::fs {

//! "C:\", "D:\", ... for every drive letter currently present, in letter order.
[[nodiscard]] std::vector<std::wstring> drive_roots();

} // namespace filetree::fs
