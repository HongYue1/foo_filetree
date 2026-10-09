// TreeView: reacting to settings changes (filter/sort relist with state restore, roots,
// row height, display options).

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include "../fs/fb2k_glue.h"

namespace filetree::view {

void TreeView::refresh_options() noexcept {
    const settings::Settings& s = settings::current();
    options_.show_hidden = s.show_hidden;
    options_.show_system = s.show_system;
    options_.files = s.files;
    options_.sort = s.sort;
    options_.rules = settings::filter_rules();
    lines_ = s.lines;
    extensions_ = s.extensions;
    show_icons_ = s.show_icons;
    mark_favourites_ = s.mark_favourites;
    follow_playing_ = s.follow_playing;
    if (mark_playing_ != s.mark_playing) {
        mark_playing_ = s.mark_playing;
        resolve_playing();
    }
    separate_favourites_ = s.separate_favourites;
    hover_highlight_ = s.hover_highlight;
    zebra_ = s.zebra;
    transparent_ = s.transparent;
    if (tooltips_ != s.tooltips) {
        tooltips_ = s.tooltips;
        update_tooltip(hover_row_);
    }
    watch_changes_ = s.watch_changes;
    try {
        favourite_paths_.clear();
        favourite_leaves_.clear();
        for (std::wstring path : s.favourites) {
            while (path.size() > 1 && path.back() == L'\\') path.pop_back();
            CharUpperBuffW(path.data(), static_cast<DWORD>(path.size()));
            const std::size_t slash = path.find_last_of(L'\\');
            favourite_leaves_.insert(slash == std::wstring::npos ? path : path.substr(slash + 1));
            favourite_paths_.insert(std::move(path));
        }
    } catch (...) {
    }
    try {
        if (s.files == fs::FileMode::playable || s.extensions == settings::Extensions::non_playable ||
            s.show_icons) {
            playable_ = fs::playable_extensions();
        }
    } catch (...) {
    }
    options_.playable = s.files == fs::FileMode::playable ? playable_ : nullptr;
}

void TreeView::on_settings_changed(std::uint32_t changes) noexcept {
    if (wnd_ == nullptr) return;
    refresh_options();
    if ((changes & settings::change_remeasure) != 0) {
        remeasure();
        on_size();
    }
    if ((changes & (settings::change_relist | settings::change_roots)) != 0) relist_all();
    if ((changes & settings::change_repaint) != 0) {
        set_colours(colours_); // line colour
        remeasure();           // line width
    }
    schedule_watch_sync(); // the Watch setting, or new roots
    InvalidateRect(wnd_, nullptr, FALSE);
}

std::wstring TreeView::upper_path(std::uint32_t node) const {
    std::wstring path;
    tree_.build_path(node, path);
    if (!path.empty()) CharUpperBuffW(path.data(), static_cast<DWORD>(path.size()));
    return path;
}

void TreeView::relist_all() noexcept {
    try {
        end_rename(false);
        restore_expand_.clear();
        restore_select_.clear();
        for (const std::uint32_t node : tree_.rows()) {
            const model::Node& n = tree_.node(node);
            if (n.has(model::node_expanded)) restore_expand_.insert(upper_path(node));
        }
        if (selected_row_ >= 0) {
            restore_select_ = upper_path(tree_.node_at_row(static_cast<std::size_t>(selected_row_)));
        }
        populate_roots();
        update_scrollbar();
        InvalidateRect(wnd_, nullptr, FALSE);
        // Roots have no listing to wait for. They are the first nodes of a fresh tree.
        const auto roots = static_cast<std::uint32_t>(tree_.node_count());
        for (std::uint32_t node = 0; node < roots; ++node) try_restore(node);
    } catch (...) {
        restore_expand_.clear();
        restore_select_.clear();
    }
}

void TreeView::try_restore(std::uint32_t node) {
    const std::wstring path = upper_path(node);
    if (!restore_top_.empty() && path == restore_top_) {
        restore_top_.clear();
        restore_top_node_ = node;
    }
    if (!restore_select_.empty() && path == restore_select_) {
        restore_select_.clear();
        if (const auto row = tree_.row_of(node)) select_row(*row);
    }
    const auto found = restore_expand_.find(path);
    if (found == restore_expand_.end()) return;
    restore_expand_.erase(found);
    expand(node);
    // Already listed (navigating through folders opened before): go on into the children now.
    // Otherwise on_listing() does when the listing arrives.
    const model::Node& n = tree_.node(node);
    if (!n.has(model::node_loaded) || n.has(model::node_loading)) return;
    const std::uint32_t first = n.first_child;
    const std::uint32_t count = n.child_count;
    for (std::uint32_t child = first; child < first + count; ++child) {
        if (restore_expand_.empty() && restore_select_.empty() && restore_top_.empty()) break;
        try_restore(child);
    }
}

} // namespace filetree::view
