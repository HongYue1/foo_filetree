#pragma once

// Undo for a Recycle Bin delete. Blocking disk work: call on a worker.

#include <string>

namespace filetree::actions {

//! Moves the most recently recycled item that was deleted from `path` back to it. Returns false
//! if there is none (network drives have no Recycle Bin), or if `path` exists again.
//!
//! Windows keeps the bin per volume and user: X:\$Recycle.Bin\<user SID>\ holds `$R<id>` (the
//! item itself, renamed) and `$I<id>` (an index record). The `$I` record is:
//!   int64 version (1 = Vista..8.1, 2 = Windows 10+), int64 size, FILETIME deleted,
//!   v1: wchar[260] original path; v2: uint32 length in chars (with the NUL), then the path.
//! Restoring is: rename `$R<id>` back to the original path (same volume: instant, also for a
//! folder), then delete `$I<id>`. This is what Explorer's "Restore" does, without its UI.
bool restore_from_recycle_bin(const std::wstring& path);

} // namespace filetree::actions
