// TreeView input: keyboard, mouse and the bound click/key actions.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <algorithm>
#include <string_view>

#include "../actions/action_settings.h"
#include "../actions/drag_out.h"
#include "../actions/playlist_send.h"

namespace filetree::view {

bool TreeView::on_key(WPARAM key) noexcept {
    const std::size_t rows = tree_.row_count();
    if (rows == 0) return false;
    const bool has_selection = selected_row_ >= 0;
    const std::size_t current = has_selection ? static_cast<std::size_t>(selected_row_) : top_row_;
    const std::uint32_t node = tree_.node_at_row(current);
    const model::Node& n = tree_.node(node);
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    const bool alt = GetKeyState(VK_MENU) < 0;

    switch (key) {
    case VK_UP: move_to(has_selection && current > 0 ? current - 1 : current, shift, ctrl); return true;
    case VK_DOWN:
        move_to(has_selection ? std::min(current + 1, rows - 1) : current, shift, ctrl);
        return true;
    // Folder jumps (user's choice over Explorer paging): PgUp goes to the parent folder, PgDn
    // to the row after the parent's subtree, i.e. the parent's next sibling (or the next row
    // further out). At a root, PgUp stays and PgDn goes to the next root.
    case VK_PRIOR:
        if (!has_selection) {
            move_to(current, shift, ctrl);
        } else if (n.parent != model::no_node) {
            if (const auto parent = tree_.row_of(n.parent)) move_to(*parent, shift, ctrl);
        }
        return true;
    case VK_NEXT: {
        if (!has_selection) {
            move_to(current, shift, ctrl);
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
        move_to(next < rows ? next : rows - 1, shift, ctrl);
        return true;
    }
    case VK_HOME: move_to(0, shift, ctrl); return true;
    case VK_END: move_to(rows - 1, shift, ctrl); return true;
    case VK_SPACE: // Ctrl+Space toggles the focus row; plain Space is type-ahead (on_char)
        if (!ctrl || alt) return false;
        toggle_row(current);
        return true;
    case 'A':
        if (!ctrl || alt || shift) return false;
        select_all();
        return true;
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
    // Explorer's keys. Other Ctrl/Alt combinations fall through to fb2k's shortcuts.
    case VK_F2:
        if (!has_selection || ctrl || alt) return false;
        begin_rename(node);
        return true;
    case VK_DELETE:
        if (!has_selection || ctrl || alt) return false;
        delete_node(node, shift);
        return true;
    case VK_F5:
        if (ctrl || alt) return false;
        refresh_open_folders();
        return true;
    case VK_F7:
        if (!has_selection || ctrl || alt || shift) return false;
        new_folder(node);
        return true;
    case 'C': // Explorer: Ctrl+C copies the item, Ctrl+Shift+C its path
        if (!has_selection || !ctrl || alt) return false;
        if (shift) {
            copy_path(node);
        } else {
            put_on_clipboard(node, false);
        }
        return true;
    case 'X':
        if (!has_selection || !ctrl || alt || shift) return false;
        put_on_clipboard(node, true);
        return true;
    case 'V':
        if (!has_selection || !ctrl || alt || shift) return false;
        paste_into(node);
        return true;
    case 'Z':
        if (!ctrl || alt || shift) return false;
        undo();
        return true;
    default:
        return false;
    }
}

namespace {

//! Explorer's pause before typing starts a new search.
constexpr DWORD typeahead_reset_ms = 1000;

[[nodiscard]] bool starts_with_ignoring_case(std::wstring_view name,
                                             std::wstring_view prefix) noexcept {
    if (prefix.size() > name.size()) return false;
    return CompareStringOrdinal(name.data(), static_cast<int>(prefix.size()), prefix.data(),
                                static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

} // namespace

bool TreeView::on_char(wchar_t ch, DWORD time) noexcept {
    // Control characters (Enter, Esc, Backspace, Ctrl+letter) are not names.
    if (ch < L' ') return false;
    if (GetKeyState(VK_CONTROL) < 0) return true; // Ctrl+Space toggles (on_key), not a search
    const std::size_t rows = tree_.row_count();
    if (rows == 0) return true;
    try {
        if (time - typeahead_time_ > typeahead_reset_ms) typeahead_.clear();
        typeahead_time_ = time;
        if (typeahead_.empty() && ch == L' ') return true;
        typeahead_.push_back(ch);

        // Typing one letter repeatedly steps through the names starting with it, as in Explorer.
        bool repeat = true;
        for (const wchar_t c : typeahead_) {
            if (CompareStringOrdinal(&c, 1, typeahead_.data(), 1, TRUE) != CSTR_EQUAL) {
                repeat = false;
                break;
            }
        }
        const std::wstring_view prefix =
            repeat ? std::wstring_view(typeahead_).substr(0, 1) : std::wstring_view(typeahead_);
        const bool has_selection = selected_row_ >= 0;
        // A longer prefix may still match the current row; a repeated letter moves on.
        const std::size_t start = !has_selection ? 0
                                  : repeat       ? static_cast<std::size_t>(selected_row_) + 1
                                                 : static_cast<std::size_t>(selected_row_);
        for (std::size_t i = 0; i < rows; ++i) {
            const std::size_t row = (start + i) % rows;
            if (starts_with_ignoring_case(tree_.node(tree_.node_at_row(row)).name_view(), prefix)) {
                select_row(row);
                break;
            }
        }
    } catch (...) {
    }
    return true;
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
    // Press and move starts a drag out instead of the click action. DragDetect returns at once
    // on movement past the system drag threshold or on button up (a plain click).
    const auto drag_detect = [&] {
        if (wnd_ == nullptr) return false;
        POINT screen{x, y};
        ClientToScreen(wnd_, &screen);
        return DragDetect(wnd_, screen) != FALSE;
    };
    // Ctrl+click toggles, Shift+click selects a range (Ctrl+Shift adds it); no action runs.
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    if (shift || ctrl) {
        if (double_click) return;
        if (shift) {
            extend_to(static_cast<std::size_t>(row), ctrl);
        } else {
            toggle_row(static_cast<std::size_t>(row));
        }
        if (tree_.is_selected(node) && drag_detect()) drag_node(node);
        return;
    }
    // A press on a row of a multi-selection keeps it, so the whole selection can be dragged;
    // releasing without a drag selects the row alone, as in Explorer.
    const bool keep = !double_click && tree_.is_selected(node) && tree_.count_selected_rows(2) > 1;
    if (keep) {
        focus_row(static_cast<std::size_t>(row));
    } else {
        // Single click always selects; a bound single-click action runs in addition, at once (no
        // double-click delay timer), so it also runs on the first click of a double click.
        select_row(static_cast<std::size_t>(row));
    }
    if (!double_click && drag_detect()) {
        drag_node(node);
        return;
    }
    if (keep) select_row(static_cast<std::size_t>(row));
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
        case actions::Kind::send:
            send_node(action, node);
            return;
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


