// Change watching: every open folder in the rows is watched (fs/watcher.h); a change re-checks
// that folder in the background (tree_view_refresh.cpp), which merges only what differs.
//
// The watch set follows the rows lazily: row changes schedule one sync shortly after, so a burst
// of expands costs one pass. Notifications are batched the same way: at most one re-check per
// folder per watch_delay_ms, however busy the folder is.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <algorithm>
#include <unordered_map>

#include "../fs/watcher.h"

namespace filetree::view {
namespace {

constexpr UINT_PTR timer_watch = 0x51;
constexpr UINT_PTR timer_watch_sync = 0x52;
constexpr UINT watch_delay_ms = 250;
constexpr UINT sync_delay_ms = 100;
constexpr std::size_t max_watches = 256; //!< handles are cheap, but not free on a server share

//! Removable and optical drives are not watched: an open handle would block Safely Remove.
bool watchable_drive(const std::wstring& path, std::array<UINT, 26>& types) noexcept {
    if (path.size() < 3 || path[1] != L':') return true; // UNC: a share, watch it
    const wchar_t letter = static_cast<wchar_t>(towupper(path[0]));
    if (letter < L'A' || letter > L'Z') return true;
    UINT& type = types[static_cast<std::size_t>(letter - L'A')];
    if (type == 0xffffffffu) {
        const wchar_t root[4] = {letter, L':', L'\\', L'\0'};
        type = GetDriveTypeW(root);
    }
    return type != DRIVE_REMOVABLE && type != DRIVE_CDROM;
}

} // namespace

void TreeView::schedule_watch_sync() noexcept {
    if (wnd_ == nullptr || sync_pending_) return;
    sync_pending_ = SetTimer(wnd_, timer_watch_sync, sync_delay_ms, nullptr) != 0;
}

void TreeView::sync_watches() noexcept {
    if (wnd_ == nullptr) return;
    if (!watch_changes_ || search_mode_) { // search results: partial folders, nothing to watch
        unwatch_all();
        return;
    }
    try {
        // What should be watched: open, listed folders on the rows.
        std::unordered_map<std::wstring, std::uint32_t> wanted;
        std::array<UINT, 26> types;
        types.fill(0xffffffffu);
        std::wstring path;
        for (const std::uint32_t node : tree_.rows()) {
            if (wanted.size() >= max_watches) break;
            const model::Node& n = tree_.node(node);
            if (!n.has(model::node_container) || !n.has(model::node_expanded) ||
                !n.has(model::node_loaded) || n.has(model::node_virtual)) {
                continue;
            }
            tree_.build_path(node, path);
            if (!watchable_drive(path, types)) continue;
            CharUpperBuffW(path.data(), static_cast<DWORD>(path.size()));
            wanted.emplace(path, node);
        }
        // Keep (with the node index refreshed: merges move nodes), drop, then add.
        std::erase_if(watched_, [&](Watched& w) {
            const auto found = wanted.find(w.path);
            if (found == wanted.end()) {
                fs::unwatch_folder(w.id);
                return true;
            }
            w.node = found->second;
            wanted.erase(found);
            return false;
        });
        for (auto& [upper, node] : wanted) {
            tree_.build_path(node, path);
            const fs::WatchId id = fs::watch_folder(path, wnd_, watch_message);
            if (id != 0) watched_.push_back({id, node, upper});
        }
    } catch (...) {
    }
}

void TreeView::unwatch_all() noexcept {
    for (const Watched& w : watched_) fs::unwatch_folder(w.id);
    watched_.clear();
    watch_dirty_.clear();
}

void TreeView::stop_watching() noexcept {
    unwatch_all();
    if (wnd_ != nullptr) {
        KillTimer(wnd_, timer_watch);
        KillTimer(wnd_, timer_watch_sync);
    }
    sync_pending_ = false;
    watch_timer_pending_ = false;
}

void TreeView::on_watch_notify(fs::WatchId id) noexcept {
    try {
        if (std::find(watch_dirty_.begin(), watch_dirty_.end(), id) == watch_dirty_.end()) {
            watch_dirty_.push_back(id);
        }
    } catch (...) {
        return;
    }
    // Not re-armed while pending: a folder that changes non-stop is still re-checked.
    if (!watch_timer_pending_) {
        watch_timer_pending_ = SetTimer(wnd_, timer_watch, watch_delay_ms, nullptr) != 0;
    }
}

bool TreeView::on_watch_timer(UINT_PTR id) noexcept {
    if (id == timer_watch_sync) {
        KillTimer(wnd_, timer_watch_sync);
        sync_pending_ = false;
        sync_watches();
        return true;
    }
    if (id != timer_watch) return false;
    KillTimer(wnd_, timer_watch);
    watch_timer_pending_ = false;
    std::vector<fs::WatchId> dirty;
    dirty.swap(watch_dirty_);
    for (const fs::WatchId watch : dirty) {
        const auto found = std::find_if(watched_.begin(), watched_.end(),
                                        [watch](const Watched& w) { return w.id == watch; });
        if (found == watched_.end()) continue;
        try {
            // The index came from the last sync; make sure it is still that folder.
            const std::uint32_t node = found->node;
            if (node >= tree_.node_count()) continue;
            const model::Node& n = tree_.node(node);
            if (!n.has(model::node_loaded) || n.has(model::node_loading)) continue;
            if (upper_path(node) != found->path) continue;
            request_check(node);
        } catch (...) {
        }
    }
    return true;
}

} // namespace filetree::view
