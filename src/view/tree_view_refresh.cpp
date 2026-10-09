// Refresh (F5): re-list every open folder in the background and merge what changed in place.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <algorithm>

#include "../fs/fb2k_glue.h"

namespace filetree::view {

void TreeView::refresh_open_folders() noexcept {
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
    if (std::any_of(pending_.begin(), pending_.end(),
                    [node](const PendingListing& p) { return p.node == node; })) {
        return; // a listing or check is already on its way
    }
    if (options_.files == fs::FileMode::playable && options_.playable == nullptr) {
        refresh_options();
    }
    tree_.build_path(node, path_);
    std::weak_ptr<TreeView*> weak = alive_;
    const std::uint64_t generation = generation_;
    auto ticket = fs::enumeration().request(
        path_, options_, [weak, node, generation](fs::Listing& listing) {
            if (const auto alive = weak.lock()) (*alive)->on_check(node, generation, listing);
        });
    pending_.push_back({node, std::move(ticket), true});
}

void TreeView::on_check(std::uint32_t node, std::uint64_t generation,
                        fs::Listing& listing) noexcept {
    std::erase_if(pending_, [node](const PendingListing& p) { return p.node == node && p.check; });
    if (generation != generation_ || wnd_ == nullptr || node >= tree_.node_count()) return;
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

        end_rename(false);
        const std::uint32_t selected =
            selected_row_ >= 0 ? tree_.node_at_row(static_cast<std::size_t>(selected_row_))
                               : model::no_node;
        const std::uint32_t top =
            top_row_ < tree_.row_count() ? tree_.node_at_row(top_row_) : model::no_node;
        const std::size_t old_top = top_row_;

        const model::Tree::MergeResult merge = tree_.merge_children(node, records_);
        // Callbacks carry the node index they were made for: listings for moved children are
        // asked for again under the new index (checks are simply dropped).
        std::vector<std::uint32_t> relist;
        std::erase_if(pending_, [&](const PendingListing& p) {
            const std::uint32_t moved = merge.map(p.node);
            if (moved == p.node) return false;
            p.ticket.cancel();
            if (!p.check && moved != model::no_node) relist.push_back(moved);
            return true;
        });
        for (const std::uint32_t moved : relist) {
            try {
                request_listing(moved);
            } catch (...) {
                tree_.fail_load(moved);
            }
        }
        pending_select_.folder = merge.map(pending_select_.folder);
        undo_.folder = merge.map(undo_.folder);
        filter_hidden_selection_ = merge.map(filter_hidden_selection_);

        // Keep the selection and the first visible row on the same items. A selected item that
        // disappeared hands the selection to the refreshed folder.
        const std::uint32_t now_selected = merge.map(selected);
        std::optional<std::size_t> selected_at;
        if (now_selected != model::no_node) selected_at = tree_.row_of(now_selected);
        if (!selected_at && selected != model::no_node && !tree_.filtered()) {
            selected_at = tree_.row_of(node);
        }
        selected_row_ = selected_at ? static_cast<std::ptrdiff_t>(*selected_at) : -1;
        const std::uint32_t now_top = merge.map(top);
        std::optional<std::size_t> top_at;
        if (now_top != model::no_node) top_at = tree_.row_of(now_top);
        top_row_ = std::min(top_at ? *top_at : old_top, max_top_row());
        hover_row_ = -1;
        update_scrollbar();
        InvalidateRect(wnd_, nullptr, FALSE);
        const std::uint32_t selected_now =
            selected_row_ >= 0 ? tree_.node_at_row(static_cast<std::size_t>(selected_row_))
                               : model::no_node;
        if (selected_now != selected) notify_selection();
    } catch (...) {
    }
}

} // namespace filetree::view
