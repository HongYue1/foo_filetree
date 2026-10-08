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
    try {
        if (s.files == fs::FileMode::playable || s.extensions == settings::Extensions::non_playable) {
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
    if (!restore_select_.empty() && path == restore_select_) {
        restore_select_.clear();
        if (const auto row = tree_.row_of(node)) select_row(*row);
    }
    if (const auto found = restore_expand_.find(path); found != restore_expand_.end()) {
        restore_expand_.erase(found);
        expand(node);
    }
}

} // namespace filetree::view
