#pragma once

// Folder change watching: ReadDirectoryChangesW, overlapped, on one completion-port thread for
// the whole component. A change in a watched folder posts `message` to `target` with the watch
// id in LPARAM; the receiver decides what to re-list (it re-checks in the background, so what a
// notification says is not parsed: any notification, or a buffer overflow, means "look again").
//
// Opening a folder can block (network), so watch() only queues the request; the thread opens it.
// unwatch() is safe at any time, also for ids that already ended (the folder was deleted). Main
// thread for watch/unwatch; the thread posts only.

#include <windows.h>

#include <cstdint>
#include <string>

namespace filetree::fs {

using WatchId = std::uint64_t;

//! Starts watching (non-recursive: names, sizes, write times). 0 when the service is down.
WatchId watch_folder(const std::wstring& path, HWND target, UINT message);
void unwatch_folder(WatchId id) noexcept;
//! Watches open now (opening or listening): for tests and the performance counters.
[[nodiscard]] std::size_t watch_count() noexcept;
//! At quit: cancels everything and stops the thread. Later watch_folder calls return 0.
void shutdown_watcher() noexcept;

} // namespace filetree::fs
