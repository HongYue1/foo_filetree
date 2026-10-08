// TreeView input: keyboard, mouse and the bound click/key actions.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <algorithm>

#include "../actions/action_settings.h"
#include "../actions/playlist_send.h"

namespace filetree::view {

bool TreeView::on_key(WPARAM key) noexcept {
    const std::size_t rows = tree_.row_count();
    if (rows == 0) return false;
    const bool has_selection = selected_row_ >= 0;
    const std::size_t current = has_selection ? static_cast<std::size_t>(selected_row_) : top_row_;
    const std::uint32_t node = tree_.node_at_row(current);
    const model::Node& n = tree_.node(node);

    switch (key) {
    case VK_UP: select_row(has_selection && current > 0 ? current - 1 : current); return true;
    case VK_DOWN: select_row(has_selection ? std::min(current + 1, rows - 1) : current); return true;
    // Folder jumps (user's choice over Explorer paging): PgUp goes to the parent folder, PgDn
    // to the row after the parent's subtree, i.e. the parent's next sibling (or the next row
    // further out). At a root, PgUp stays and PgDn goes to the next root.
    case VK_PRIOR:
        if (!has_selection) {
            select_row(current);
        } else if (n.parent != model::no_node) {
            if (const auto parent = tree_.row_of(n.parent)) select_row(*parent);
        }
        return true;
    case VK_NEXT: {
        if (!has_selection) {
            select_row(current);
            return true;
        }
        // Rows are in display order, so the parent's subtree ends at the first later row that
        // is shallower than the current one.
        const std::uint16_t depth = n.depth;
        std::size_t next = current + 1;
        if (depth == 0) {
            while (next < rows && tree_.node(tree_.node_at_row(next)).depth > 0) ++next;
        } else {
            while (next < rows && tree_.node(tree_.node_at_row(next)).depth >= depth) ++next;
        }
        select_row(next < rows ? next : rows - 1);
        return true;
    }
    case VK_HOME: select_row(0); return true;
    case VK_END: select_row(rows - 1); return true;
    case VK_LEFT:
        if (!has_selection) {
            select_row(current);
        } else if (n.has(model::node_expanded)) {
            collapse(node);
        } else if (n.parent != model::no_node) {
            if (const auto parent = tree_.row_of(n.parent)) select_row(*parent);
        }
        return true;
    case VK_RIGHT:
        if (!has_selection) {
            select_row(current);
        } else if (n.has(model::node_container)) {
            if (!n.has(model::node_expanded) || n.has(model::node_load_failed)) {
                expand(node);
            } else if (current + 1 < rows &&
                       tree_.node(tree_.node_at_row(current + 1)).parent == node) {
                select_row(current + 1);
            }
        }
        return true;
    case VK_ADD:
        if (has_selection) expand(node);
        return true;
    case VK_SUBTRACT:
        if (has_selection) collapse(node);
        return true;
    case VK_RETURN:
        if (has_selection) run_gesture(actions::Gesture::enter, node);
        return true;
    default:
        return false;
    }
}

void TreeView::on_button_down(int x, int y, bool double_click) noexcept {
    if (wnd_ != nullptr && GetFocus() != wnd_) SetFocus(wnd_);
    const std::ptrdiff_t row = row_at(y);
    if (row < 0) return;
    const std::uint32_t node = tree_.node_at_row(static_cast<std::size_t>(row));
    const model::Node& n = tree_.node(node);

    const int expander_x = expander_left(n.depth);
    const bool on_expander = n.has(model::node_container) && x >= expander_x &&
                             x < expander_x + metrics_.indent;
    if (on_expander) {
        toggle(node);
        return;
    }
    // Single click always selects; a bound single-click action runs in addition, at once (no
    // double-click delay timer), so it also runs on the first click of a double click.
    select_row(static_cast<std::size_t>(row));
    run_gesture(double_click ? actions::Gesture::double_click : actions::Gesture::single_click,
                node);
}

void TreeView::on_middle_button(int y) noexcept {
    if (wnd_ != nullptr && GetFocus() != wnd_) SetFocus(wnd_);
    const std::ptrdiff_t row = row_at(y);
    if (row < 0) return;
    select_row(static_cast<std::size_t>(row));
    run_gesture(actions::Gesture::middle_click, tree_.node_at_row(static_cast<std::size_t>(row)));
}

void TreeView::run_gesture(actions::Gesture gesture, std::uint32_t node) noexcept {
    try {
        const model::Node& n = tree_.node(node);
        const bool folder = n.has(model::node_container);
        const actions::Action& action = actions::bindings().lookup(gesture, folder);
        switch (action.kind) {
        case actions::Kind::none:
            return;
        case actions::Kind::toggle:
            if (folder) toggle(node);
            return;
        case actions::Kind::send: {
            actions::SendRequest request;
            request.action = action;
            tree_.build_path(node, request.path);
            std::wstring_view name = n.name_view();
            if (n.has(model::node_root) && name.size() == 3 && name[1] == L':') {
                name.remove_suffix(1);
            }
            request.display_name.assign(name);
            request.is_folder = folder;
            request.shift = GetKeyState(VK_SHIFT) < 0;
            request.ctrl = GetKeyState(VK_CONTROL) < 0;
            request.parent = wnd_;
            actions::send(request);
            return;
        }
        }
    } catch (...) {
    }
}

void TreeView::on_mouse_move(int, int y) noexcept {
    if (!tracking_mouse_ && wnd_ != nullptr) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, wnd_, 0};
        tracking_mouse_ = TrackMouseEvent(&track) != FALSE;
    }
    const std::ptrdiff_t row = row_at(y);
    if (row == hover_row_) return;
    if (hover_row_ >= 0) invalidate_row(static_cast<std::size_t>(hover_row_));
    hover_row_ = row;
    if (row >= 0) invalidate_row(static_cast<std::size_t>(row));
}

void TreeView::on_mouse_leave() noexcept {
    tracking_mouse_ = false;
    if (hover_row_ >= 0) invalidate_row(static_cast<std::size_t>(hover_row_));
    hover_row_ = -1;
}

} // namespace filetree::view
