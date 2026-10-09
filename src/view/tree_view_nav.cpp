// TreeView navigation: the selection as crumbs and paths, and going to a typed path (address
// bar, history). Going to a path reuses the relist restore: the folders on the way are queued for
// expansion and the target for selection, and listings fill them in as they arrive.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include "../model/status_text.h"

#include <algorithm>

namespace filetree::view {
namespace {

//! What a user may type: quotes, forward slashes, %variables%, "c:" without a backslash, a
//! trailing backslash. Empty when it is not an absolute drive path.
std::wstring normalise(std::wstring_view input) {
    while (!input.empty() && (input.front() == L' ' || input.front() == L'"')) input.remove_prefix(1);
    while (!input.empty() && (input.back() == L' ' || input.back() == L'"')) input.remove_suffix(1);
    if (input.empty()) return {};
    std::wstring text(input);
    std::replace(text.begin(), text.end(), L'/', L'\\');
    if (text.find(L'%') != std::wstring::npos) {
        std::wstring expanded(32768, L'\0');
        const DWORD length = ExpandEnvironmentStringsW(text.c_str(), expanded.data(),
                                                       static_cast<DWORD>(expanded.size()));
        if (length > 0 && length <= expanded.size()) {
            expanded.resize(length - 1);
            text = std::move(expanded);
        }
    }
    if (text.size() < 2 || text[1] != L':' || !iswalpha(text[0])) return {};
    if (text.size() == 2) text.push_back(L'\\');
    if (text[2] != L'\\') return {};
    // Resolves "." and ".." without touching the disk.
    std::wstring full(32768, L'\0');
    const DWORD length = GetFullPathNameW(text.c_str(), static_cast<DWORD>(full.size()),
                                          full.data(), nullptr);
    if (length == 0 || length >= full.size()) return {};
    full.resize(length);
    while (full.size() > 3 && full.back() == L'\\') full.pop_back();
    CharUpperBuffW(full.data(), static_cast<DWORD>(full.size()));
    return full;
}

} // namespace

void TreeView::notify_selection() noexcept {
    acc_focus_changed();
    if (!selection_listener_) return;
    try {
        selection_listener_();
    } catch (...) {
    }
}

void TreeView::selection_crumbs(std::vector<Crumb>& out) const {
    out.clear();
    if (selected_row_ < 0 || static_cast<std::size_t>(selected_row_) >= tree_.row_count()) return;
    for (std::uint32_t node = tree_.node_at_row(static_cast<std::size_t>(selected_row_));
         node != model::no_node; node = tree_.node(node).parent) {
        out.push_back({std::wstring(model::display_name(tree_.node(node))), node});
    }
    std::reverse(out.begin(), out.end());
}

std::wstring TreeView::selected_path() const {
    std::wstring path;
    if (selected_row_ >= 0 && static_cast<std::size_t>(selected_row_) < tree_.row_count()) {
        tree_.build_path(tree_.node_at_row(static_cast<std::size_t>(selected_row_)), path);
    }
    return path;
}

void TreeView::select_node(std::uint32_t node) noexcept {
    if (node >= tree_.node_count()) return;
    if (const auto row = tree_.row_of(node)) select_row(*row);
}

void TreeView::select_parent() noexcept {
    if (selected_row_ < 0 || static_cast<std::size_t>(selected_row_) >= tree_.row_count()) return;
    const std::uint32_t parent =
        tree_.node(tree_.node_at_row(static_cast<std::size_t>(selected_row_))).parent;
    if (parent != model::no_node) select_node(parent);
}

bool TreeView::navigate_to(std::wstring_view input, bool expand_target) noexcept {
    try {
        const std::wstring target = normalise(input);
        if (target.empty()) return false;
        // Roots are the first nodes of the tree (populate_roots adds them before anything else).
        std::uint32_t root = model::no_node;
        for (std::uint32_t node = 0;
             node < tree_.node_count() && tree_.node(node).has(model::node_root); ++node) {
            const std::wstring path = upper_path(node);
            if (target.compare(0, path.size(), path) == 0 &&
                (target.size() == path.size() || path.back() == L'\\' ||
                 target[path.size()] == L'\\')) {
                root = node;
                break;
            }
        }
        if (root == model::no_node) return false;

        end_rename(false);
        restore_expand_.clear();
        restore_select_ = target;
        // Every folder on the way: the root, then each prefix ending before a backslash.
        const std::wstring root_path = upper_path(root);
        restore_expand_.insert(root_path);
        for (std::size_t at = target.find(L'\\', root_path.size()); at != std::wstring::npos;
             at = target.find(L'\\', at + 1)) {
            restore_expand_.insert(target.substr(0, at));
        }
        if (expand_target) restore_expand_.insert(target);
        try_restore(root);
        return true;
    } catch (...) {
        restore_expand_.clear();
        restore_select_.clear();
        return false;
    }
}

void TreeView::capture_state(settings::PanelState& out) const {
    out = {};
    std::wstring path;
    for (const std::uint32_t node : tree_.rows()) {
        const model::Node& n = tree_.node(node);
        if (!n.has(model::node_container) || !n.has(model::node_expanded)) continue;
        tree_.build_path(node, path);
        out.expanded.push_back(path);
    }
    for (const std::wstring& pending : restore_expand_) out.expanded.push_back(pending);
    if (selected_row_ >= 0 && static_cast<std::size_t>(selected_row_) < tree_.row_count()) {
        tree_.build_path(tree_.node_at_row(static_cast<std::size_t>(selected_row_)), out.selected);
    } else {
        out.selected = restore_select_;
    }
    if (!restore_top_.empty()) {
        out.top = restore_top_;
    } else if (top_row_ < tree_.row_count()) {
        tree_.build_path(tree_.node_at_row(top_row_), out.top);
    }
}

void TreeView::restore_state(const settings::PanelState& state) noexcept {
    try {
        const auto upper = [](std::wstring text) {
            if (!text.empty()) CharUpperBuffW(text.data(), static_cast<DWORD>(text.size()));
            return text;
        };
        end_rename(false);
        restore_expand_.clear();
        for (const std::wstring& path : state.expanded) restore_expand_.insert(upper(path));
        restore_select_ = upper(state.selected);
        restore_top_ = upper(state.top);
        restore_top_node_ = model::no_node;
        for (std::uint32_t node = 0;
             node < tree_.node_count() && tree_.node(node).has(model::node_root); ++node) {
            try_restore(node);
        }
        apply_restore_top();
    } catch (...) {
        restore_expand_.clear();
        restore_select_.clear();
        restore_top_.clear();
    }
}

void TreeView::apply_restore_top() noexcept {
    if (restore_top_node_ == model::no_node) return;
    if (const auto row = tree_.row_of(restore_top_node_)) {
        const std::size_t top = std::min(*row, max_top_row());
        if (top != top_row_) {
            top_row_ = top;
            update_scrollbar();
            InvalidateRect(wnd_, nullptr, FALSE);
        }
    }
    if (restore_expand_.empty() && restore_select_.empty()) restore_top_node_ = model::no_node;
}

model::Summary TreeView::status_summary() const noexcept {
    const std::uint32_t focus =
        selected_row_ >= 0 && static_cast<std::size_t>(selected_row_) < tree_.row_count()
            ? tree_.node_at_row(static_cast<std::size_t>(selected_row_))
            : model::no_node;
    return model::summarize(tree_, focus);
}

std::wstring TreeView::counters_text() const {
    std::wstring out = L"Rows: " + std::to_wstring(tree_.row_count());
    out += L"\nNodes: " + std::to_wstring(tree_.node_count());
    out += L"\nMemory: " + model::format_size(tree_.memory_bytes());
    out += L"\nWatched folders: " + std::to_wstring(watched_.size());
    out += L"\nListings in flight: " + std::to_wstring(pending_.size());
    return out;
}

} // namespace filetree::view
