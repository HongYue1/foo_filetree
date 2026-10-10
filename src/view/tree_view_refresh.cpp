// Refresh (F5): re-list every open folder in the background and merge what changed in place.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <algorithm>

#include "../fs/fb2k_glue.h"

namespace filetree::view {

void TreeView::refresh_open_folders() noexcept {
    if (search_mode_) { // F5 on search results: search again
        const std::wstring text = search_text_;
        start_search(text);
        return;
    }
    try {
        for (std::size_t row = 0; row < tree_.row_count(); ++row) {
            const std::uint32_t node = tree_.node_at_row(row);
            const model::Node& n = tree_.node(node);
            if (!n.has(model::node_container) || !n.has(model::node_expanded)) continue;
            if (n.has(model::node_load_failed)) {
                retry_failed(node);
            } else if (n.has(model::node_loaded) && !n.has(model::node_loading)) {
                request_check(node);
            }
        }
    } catch (...) {
    }
}

void TreeView::retry_failed(std::uint32_t node) noexcept {
    const model::Tree::ReloadResult result = tree_.reload(node);
    apply_splice(result.splice);
    if (result.needs_load) {
        try {
            request_listing(node);
        } catch (...) {
            tree_.fail_load(node);
        }
    }
    if (const auto row = tree_.row_of(node)) invalidate_row(*row);
}

void TreeView::request_check(std::uint32_t node) {
    const auto in_flight = std::find_if(pending_.begin(), pending_.end(),
                                        [node](const PendingListing& p) { return p.node == node; });
    if (in_flight != pending_.end()) {
        // Already on its way, but it may have read the folder before this change (a folder
        // deleted while watched stays listed until its handle closes): go again afterwards.
        in_flight->again = true;
        return;
    }
    if (options_.files == fs::FileMode::playable && options_.playable == nullptr) {
        refresh_options();
    }
    tree_.build_path(node, path_);
    std::weak_ptr<TreeView*> weak = alive_;
    const std::uint64_t generation = generation_;
    auto ticket = fs::enumeration().request(
        path_, options_for(node), [weak, node, generation](fs::Listing& listing) {
            if (const auto alive = weak.lock()) (*alive)->on_check(node, generation, listing);
        });
    pending_.push_back({node, std::move(ticket), true});
}

void TreeView::request_probe(std::uint32_t node, const fs::Listing& listing) {
    if (!options_.hide_empty || options_.probe_types == nullptr || listing.probed ||
        listing.error != ERROR_SUCCESS || tree_.node(node).has(model::node_virtual)) {
        return;
    }
    const bool folders = std::any_of(listing.items.begin(), listing.items.end(), [](const auto& item) {
        return (item.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    });
    if (!folders && listing.skipped_empty == 0) return;
    if (std::any_of(pending_.begin(), pending_.end(),
                    [node](const PendingListing& p) { return p.node == node; })) {
        return; // a listing or check is on its way and probes when it lands
    }
    tree_.build_path(node, path_);
    fs::EnumOptions options = options_;
    options.probe = true;
    std::weak_ptr<TreeView*> weak = alive_;
    const std::uint64_t generation = generation_;
    auto ticket = fs::enumeration().request(
        path_, std::move(options), [weak, node, generation](fs::Listing& result) {
            if (const auto alive = weak.lock()) (*alive)->on_check(node, generation, result);
        });
    // As a check: a merge that moves `node` re-requests a check, which probes again.
    pending_.push_back({node, std::move(ticket), true});
}

void TreeView::on_check(std::uint32_t node, std::uint64_t generation,
                        fs::Listing& listing) noexcept {
    bool again = false;
    std::erase_if(pending_, [node, &again](const PendingListing& p) {
        if (p.node != node || !p.check) return false;
        again = again || p.again;
        return true;
    });
    if (generation != generation_ || wnd_ == nullptr || node >= tree_.node_count()) return;
    merge_listing(node, listing);
    apply_pending_select(node); // after rename/delete (reload_and_select)
    try {
        if (again) {
            request_check(node);
        } else {
            request_probe(node, listing);
        }
    } catch (...) {
    }
}

void TreeView::merge_listing(std::uint32_t node, fs::Listing& listing) noexcept {
    // Gone meanwhile (an orphan has no row) or reloading: nothing to merge into. A folder that
    // vanished is removed by its parent's check.
    const model::Node& n = tree_.node(node);
    if (listing.error != ERROR_SUCCESS || !n.has(model::node_loaded) ||
        n.has(model::node_loading) || !tree_.row_of(node)) {
        return;
    }
    try {
        listing.to_records(records_);
        if (tree_.children_match(node, records_)) return;

        const std::uint32_t selected =
            selected_row_ >= 0 ? tree_.node_at_row(static_cast<std::size_t>(selected_row_))
                               : model::no_node;
        const std::uint32_t top =
            top_row_ < tree_.row_count() ? tree_.node_at_row(top_row_) : model::no_node;
        const std::size_t old_top = top_row_;

        const model::Tree::MergeResult merge = tree_.merge_children(node, records_);
        schedule_watch_sync(); // moved children: refresh the watch set's node indices
        // Callbacks carry the node index they were made for: listings and checks for moved
        // children are asked for again under the new index. Dropping a check would lose the
        // change it was sent for (a parent's check usually lands first: its child's time changed).
        std::vector<std::uint32_t> relist;
        std::vector<std::uint32_t> recheck;
        std::erase_if(pending_, [&](const PendingListing& p) {
            const std::uint32_t moved = merge.map(p.node);
            if (moved == p.node) return false;
            p.ticket.cancel();
            if (moved != model::no_node) (p.check ? recheck : relist).push_back(moved);
            return true;
        });
        for (const std::uint32_t moved : relist) {
            try {
                request_listing(moved);
            } catch (...) {
                tree_.fail_load(moved);
            }
        }
        // Open folders that changed on disk while their parent was closed (not watched).
        for (const std::uint32_t stale : merge.stale) {
            if (std::find(recheck.begin(), recheck.end(), stale) == recheck.end()) {
                recheck.push_back(stale);
            }
        }
        for (const std::uint32_t moved : recheck) {
            try {
                request_check(moved);
            } catch (...) {
            }
        }
        pending_select_.folder = merge.map(pending_select_.folder);
        undo_.folder = merge.map(undo_.folder);
        filter_hidden_selection_ = merge.map(filter_hidden_selection_);
        restore_top_node_ = merge.map(restore_top_node_);
        if (!cut_nodes_.empty()) {
            for (std::uint32_t& cut : cut_nodes_) cut = merge.map(cut);
            std::erase(cut_nodes_, model::no_node);
            std::sort(cut_nodes_.begin(), cut_nodes_.end());
        }
        if (edit_ != nullptr) edit_node_ = merge.map(edit_node_);

        // Keep the selection and the first visible row on the same items. A selected item that
        // disappeared hands the selection to the refreshed folder.
        const std::uint32_t now_selected = merge.map(selected);
        std::optional<std::size_t> selected_at;
        if (now_selected != model::no_node) selected_at = tree_.row_of(now_selected);
        if (!selected_at && selected != model::no_node && !tree_.filtered()) {
            selected_at = tree_.row_of(node);
        }
        selected_row_ = selected_at ? static_cast<std::ptrdiff_t>(*selected_at) : -1;
        if (selected_at && now_selected == model::no_node && tree_.count_selected_rows(1) == 0) {
            tree_.set_selected(node, true);
            tree_.set_anchor(node);
        }
        const std::uint32_t now_top = merge.map(top);
        std::optional<std::size_t> top_at;
        if (now_top != model::no_node) top_at = tree_.row_of(now_top);
        top_row_ = std::min(top_at ? *top_at : old_top, max_top_row());
        hover_row_ = -1;
        update_scrollbar();
        InvalidateRect(wnd_, nullptr, FALSE);
        follow_rename(); // a change in the folder (often a watch notification) keeps the editor
        resolve_playing();
        const std::uint32_t selected_now =
            selected_row_ >= 0 ? tree_.node_at_row(static_cast<std::size_t>(selected_row_))
                               : model::no_node;
        if (selected_now != selected) notify_selection();
    } catch (...) {
    }
}

} // namespace filetree::view
