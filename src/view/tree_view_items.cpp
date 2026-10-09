// TreeView operations on items: send, drag, clipboard, copy path, delete, properties, save as
// playlist. Each takes the node it was invoked on and acts on the whole selection when that node
// is part of it (or is the focus row), else on the node alone (actions_for).

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <algorithm>

#include "../actions/drag_out.h"
#include "../actions/file_ops.h"
#include "../actions/playlist_send.h"
#include "../actions/shell_ops.h"

namespace filetree::view {
namespace {

[[nodiscard]] std::wstring_view parent_part(std::wstring_view path) noexcept {
    const std::size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring_view::npos ? std::wstring_view{} : path.substr(0, slash);
}

[[nodiscard]] bool same_text(std::wstring_view a, std::wstring_view b) noexcept {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

//! A name usable for a playlist or file: the display name without "\" or ":".
[[nodiscard]] std::wstring plain_name(const model::Node& node) {
    std::wstring name(model::display_name(node));
    while (!name.empty() && (name.back() == L'\\' || name.back() == L':')) name.pop_back();
    return name;
}

} // namespace

void TreeView::actions_for(std::uint32_t node, std::vector<std::uint32_t>& out) const {
    out.clear();
    if (node == model::no_node || node >= tree_.node_count()) return;
    const bool focus = selected_row_ >= 0 &&
                       static_cast<std::size_t>(selected_row_) < tree_.row_count() &&
                       tree_.node_at_row(static_cast<std::size_t>(selected_row_)) == node;
    if (tree_.is_selected(node) || focus) tree_.selected_nodes(out);
    if (out.empty()) out.push_back(node);
}

void TreeView::paths_of(const std::vector<std::uint32_t>& nodes,
                        std::vector<std::wstring>& out) const {
    out.clear();
    out.reserve(nodes.size());
    for (const std::uint32_t node : nodes) tree_.build_path(node, out.emplace_back());
}

bool TreeView::same_parent(const std::vector<std::uint32_t>& nodes) const noexcept {
    if (nodes.empty()) return false;
    const std::uint32_t parent = tree_.node(nodes[0]).parent;
    if (parent == model::no_node) return nodes.size() == 1;
    return std::all_of(nodes.begin(), nodes.end(),
                       [&](std::uint32_t n) { return tree_.node(n).parent == parent; });
}

void TreeView::send_node(const actions::Action& action, std::uint32_t node) noexcept {
    try {
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        actions::SendRequest request;
        request.action = action;
        request.items.reserve(nodes.size());
        for (const std::uint32_t n : nodes) {
            actions::SendItem& item = request.items.emplace_back();
            tree_.build_path(n, item.path);
            item.is_folder = tree_.node(n).has(model::node_container);
        }
        // A new playlist is named after the item, or after the folder holding several.
        if (nodes.size() == 1) {
            request.display_name = plain_name(tree_.node(nodes[0]));
        } else if (same_parent(nodes)) {
            request.display_name = plain_name(tree_.node(tree_.node(nodes[0]).parent));
        }
        request.shift = GetKeyState(VK_SHIFT) < 0;
        request.ctrl = GetKeyState(VK_CONTROL) < 0;
        request.parent = wnd_;
        actions::send(request);
    } catch (...) {
    }
}

void TreeView::drag_node(std::uint32_t node) noexcept {
    try {
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        std::vector<std::wstring> paths;
        paths_of(nodes, paths);
        actions::drag_out(wnd_, paths);
    } catch (...) {
    }
    // The drag loop swallowed the mouse messages: the hover row is stale.
    on_mouse_leave();
}

void TreeView::open_in_explorer(std::uint32_t node) noexcept {
    try {
        std::wstring path;
        tree_.build_path(node, path);
        actions::open_in_explorer(std::move(path), tree_.node(node).has(model::node_container));
    } catch (...) {
    }
}

void TreeView::copy_path(std::uint32_t node) noexcept {
    try {
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        std::wstring text;
        for (const std::uint32_t n : nodes) {
            if (!text.empty()) text += L"\r\n";
            tree_.build_path(n, path_);
            text += path_;
        }
        if (!actions::copy_text(wnd_, text)) MessageBeep(MB_ICONWARNING);
    } catch (...) {
    }
}

void TreeView::put_on_clipboard(std::uint32_t node, bool cut) noexcept {
    try {
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        // A drive cannot be moved; a favourite root is a real folder and can.
        for (const std::uint32_t n : nodes) {
            const model::Node& item = tree_.node(n);
            if (item.parent == model::no_node && !item.has(model::node_favourite)) {
                MessageBeep(MB_ICONWARNING);
                return;
            }
        }
        std::vector<std::wstring> paths;
        paths_of(nodes, paths);
        if (!actions::set_clipboard_files(paths, cut)) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        if (!cut) nodes.clear();
        set_cut(std::move(nodes));
    } catch (...) {
    }
}

bool TreeView::is_cut(std::uint32_t node) const noexcept {
    return !cut_nodes_.empty() && std::binary_search(cut_nodes_.begin(), cut_nodes_.end(), node);
}

void TreeView::set_cut(std::vector<std::uint32_t> nodes) noexcept {
    if (wnd_ == nullptr) return;
    // A few rows: repaint them; many: repaint everything (cheaper than a row search each).
    const auto repaint = [this](const std::vector<std::uint32_t>& list) {
        if (list.size() > 8) {
            InvalidateRect(wnd_, nullptr, FALSE);
            return;
        }
        for (const std::uint32_t n : list) {
            if (n >= tree_.node_count()) continue;
            if (const auto row = tree_.row_of(n)) invalidate_row(*row);
        }
    };
    repaint(cut_nodes_);
    std::sort(nodes.begin(), nodes.end());
    cut_nodes_ = std::move(nodes);
    if (!cut_nodes_.empty()) {
        cut_sequence_ = GetClipboardSequenceNumber();
        repaint(cut_nodes_);
        // Told when anything else takes the clipboard (only while a cut is shown).
        if (!clipboard_listening_) clipboard_listening_ = AddClipboardFormatListener(wnd_) != FALSE;
    } else if (clipboard_listening_) {
        RemoveClipboardFormatListener(wnd_);
        clipboard_listening_ = false;
    }
}

void TreeView::on_clipboard_update() noexcept {
    if (!cut_nodes_.empty() && GetClipboardSequenceNumber() != cut_sequence_) set_cut({});
}

void TreeView::show_properties(std::uint32_t node) noexcept {
    try {
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        std::vector<std::wstring> paths;
        paths_of(nodes, paths);
        actions::show_properties(paths, wnd_);
    } catch (...) {
    }
}

void TreeView::show_selected_properties() noexcept {
    if (selected_row_ < 0) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    show_properties(tree_.node_at_row(static_cast<std::size_t>(selected_row_)));
}

void TreeView::save_as_playlist(std::uint32_t node) noexcept {
    try {
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        std::vector<std::wstring> paths;
        paths_of(nodes, paths);
        // One folder: start in it, named after it. Several items: in (and after) their folder.
        std::wstring start = paths[0];
        std::wstring name = plain_name(tree_.node(nodes[0]));
        const bool one_folder = nodes.size() == 1 && tree_.node(nodes[0]).has(model::node_container);
        if (!one_folder) {
            start.assign(parent_part(paths[0]));
            const std::uint32_t parent = tree_.node(nodes[0]).parent;
            if (nodes.size() > 1 && same_parent(nodes) && parent != model::no_node) {
                name = plain_name(tree_.node(parent));
            }
        }
        if (name.empty()) name = L"Playlist";
        std::wstring file;
        if (actions::pick_playlist_file(wnd_, start, name, file)) {
            actions::save_as_playlist(paths, std::move(file), wnd_);
        }
    } catch (...) {
    }
}

void TreeView::delete_node(std::uint32_t node, bool permanent) noexcept {
    try {
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        for (const std::uint32_t n : nodes) {
            if (tree_.node(n).has(model::node_root) || tree_.node(n).parent == model::no_node) {
                MessageBeep(MB_ICONWARNING); // drives and favourite roots are not deleted here
                return;
            }
        }
        // Afterwards select what Explorer would: the next unselected sibling of the last item
        // in the focus item's folder, else the previous one, else the folder.
        const std::uint32_t parent = tree_.node(node).parent;
        const model::Node& p = tree_.node(parent);
        const std::uint32_t end = p.first_child + p.child_count;
        const auto deleted = [&](std::uint32_t n) {
            return std::find(nodes.begin(), nodes.end(), n) != nodes.end();
        };
        std::wstring next;
        for (std::uint32_t n = node + 1; n < end && next.empty(); ++n) {
            if (!deleted(n)) next.assign(tree_.node(n).name_view());
        }
        for (std::uint32_t n = node; n > p.first_child && next.empty(); --n) {
            if (!deleted(n - 1)) next.assign(tree_.node(n - 1).name_view());
        }
        // Other folders the items came from are re-checked (watching may be off).
        std::vector<std::uint32_t> others;
        for (const std::uint32_t n : nodes) {
            const std::uint32_t folder = tree_.node(n).parent;
            if (folder != parent && std::find(others.begin(), others.end(), folder) == others.end()) {
                others.push_back(folder);
            }
        }
        std::vector<std::wstring> paths;
        paths_of(nodes, paths);
        // Undo restores every recycled item (the Recycle Bin lookup is by original path) and
        // selects the focus item again.
        std::wstring name = nodes.size() == 1 ? std::wstring(tree_.node(node).name_view()) : L"";
        std::wstring focus_name(tree_.node(node).name_view());
        std::vector<std::wstring> undo_paths = paths;
        actions::delete_paths(
            std::move(paths), permanent, wnd_,
            guard([parent, others = std::move(others), next = std::move(next),
                   name = std::move(name), focus_name = std::move(focus_name),
                   undo_paths = std::move(undo_paths),
                   permanent](TreeView& view, actions::ShellResult result) {
                // If the delete was cancelled the items are still there: keep the selection.
                if (!result.ran) return;
                // Cancelled part-way: some items went; undo restores those (the rest are skipped).
                if (!permanent) {
                    UndoRecord record;
                    record.kind = UndoRecord::Kind::recycle;
                    record.folder = parent;
                    view.tree_.build_path(parent, record.folder_path);
                    record.name = focus_name;
                    record.paths = undo_paths;
                    view.undo_ = std::move(record);
                }
                for (const std::uint32_t folder : others) view.check_if_open(folder);
                view.reload_and_select(parent, name, next);
            }));
    } catch (...) {
    }
}

void TreeView::reload_and_select(std::uint32_t folder, std::wstring name, std::wstring fallback,
                                 bool rename) noexcept {
    if (folder >= tree_.node_count()) return;
    const model::Node& f = tree_.node(folder);
    if (f.has(model::node_loaded) && !f.has(model::node_loading) &&
        f.has(model::node_expanded) && tree_.row_of(folder)) {
        // Open and listed: merge the change in place (no collapse, no scroll jump). A check
        // already in flight may predate the change, so it is replaced.
        try {
            std::erase_if(pending_, [folder](const PendingListing& p) {
                if (!p.check || p.node != folder) return false;
                p.ticket.cancel();
                return true;
            });
            pending_select_ = {folder, std::move(name), std::move(fallback), rename};
            request_check(folder);
            return;
        } catch (...) {
            pending_select_ = {};
        }
    }
    const model::Tree::ReloadResult result = tree_.reload(folder);
    // Listings for the forgotten children will be ignored; stop them early.
    std::erase_if(pending_, [this](const PendingListing& p) {
        if (p.check || tree_.node(p.node).has(model::node_loading)) return false;
        p.ticket.cancel();
        return true;
    });
    apply_splice(result.splice);
    pending_select_ = {folder, std::move(name), std::move(fallback), rename};
    if (result.needs_load) {
        try {
            request_listing(folder);
        } catch (...) {
            tree_.fail_load(folder);
        }
    }
    if (const auto row = tree_.row_of(folder)) invalidate_row(*row);
    // Not expanded: nothing to wait for. Still loading: on_listing finishes the job.
    if (!tree_.node(folder).has(model::node_loading)) apply_pending_select(folder);
}

void TreeView::apply_pending_select(std::uint32_t folder) noexcept {
    if (pending_select_.folder != folder) return;
    PendingSelect select = std::move(pending_select_);
    pending_select_ = {};
    std::uint32_t target = model::no_node;
    if (!select.name.empty()) target = tree_.find_child(folder, select.name);
    if (target == model::no_node && !select.fallback.empty()) {
        target = tree_.find_child(folder, select.fallback);
    }
    const bool rename = select.rename && target != model::no_node;
    if (target == model::no_node) target = folder;
    if (const auto row = tree_.row_of(target)) {
        select_row(*row);
        if (rename) begin_rename(target);
    }
}

} // namespace filetree::view
