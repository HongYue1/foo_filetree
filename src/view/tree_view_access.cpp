// TreeView accessibility (M8c): the AccessSource side of view/accessible.h, plus the WinEvents
// screen readers follow (focus, selection, expand / collapse, rows reordered).

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

namespace filetree::view {

bool TreeView::on_get_object(WPARAM wp, LPARAM lp, LRESULT& result) noexcept {
    if (static_cast<DWORD>(lp) != static_cast<DWORD>(OBJID_CLIENT) || wnd_ == nullptr) return false;
    if (accessible_ == nullptr) accessible_ = TreeAccessible::create(wnd_, *this);
    if (accessible_ == nullptr) return false;
    result = accessible_->answer(wp);
    return true;
}

void TreeView::release_accessible() noexcept {
    if (accessible_ == nullptr) return;
    // Clients still holding it get RPC_E_DISCONNECTED from now on.
    accessible_->detach();
    accessible_ = nullptr;
}

void TreeView::acc_event(DWORD event, std::ptrdiff_t row) const noexcept {
    if (wnd_ == nullptr) return;
    NotifyWinEvent(event, wnd_, OBJID_CLIENT, row >= 0 ? static_cast<LONG>(row + 1) : CHILDID_SELF);
}

void TreeView::acc_focus_changed() const noexcept {
    if (!focused_ || selected_row_ < 0) return;
    acc_event(EVENT_OBJECT_FOCUS, selected_row_);
    if (tree_.selection_hint() <= 1) acc_event(EVENT_OBJECT_SELECTION, selected_row_);
}

std::size_t TreeView::acc_row_count() const noexcept { return tree_.row_count(); }

bool TreeView::acc_row(std::size_t row, AccessRow& out) const {
    if (row >= tree_.row_count()) return false;
    const model::Node& node = tree_.node(tree_.node_at_row(row));
    out.name.assign(node.has(model::node_root) ? model::display_name(node) : shown_name(node));
    out.level = node.depth;
    out.container = node.has(model::node_container);
    out.expanded = node.has(model::node_expanded);
    out.selected = node.has(model::node_selected);
    return true;
}

bool TreeView::acc_row_rect(std::size_t row, RECT& out) const noexcept {
    if (row >= tree_.row_count() || row < top_row_) return false;
    const int top = row_top(row);
    if (top >= client_height_) return false;
    out = RECT{0, top, client_width_, top + metrics_.row_height};
    return true;
}

std::ptrdiff_t TreeView::acc_focus_row() const noexcept { return selected_row_; }

bool TreeView::acc_has_focus() const noexcept { return focused_; }

std::ptrdiff_t TreeView::acc_row_at(POINT client) const noexcept { return row_at(client.y); }

void TreeView::acc_selected_rows(std::vector<std::size_t>& out) const {
    out.clear();
    const auto rows = tree_.rows();
    if (tree_.selection_hint() == 0) return;
    for (std::size_t row = 0; row < rows.size(); ++row) {
        if (tree_.node(rows[row]).has(model::node_selected)) out.push_back(row);
    }
}

void TreeView::acc_select(std::size_t row, long flags) noexcept {
    if (row >= tree_.row_count() || wnd_ == nullptr) return;
    if ((flags & SELFLAG_TAKEFOCUS) != 0 && GetFocus() != wnd_) SetFocus(wnd_);
    if ((flags & SELFLAG_TAKESELECTION) != 0) {
        select_row(row);
        return;
    }
    if ((flags & SELFLAG_EXTENDSELECTION) != 0) {
        extend_to(row, (flags & SELFLAG_ADDSELECTION) != 0);
        return;
    }
    const std::uint32_t node = tree_.node_at_row(row);
    if ((flags & (SELFLAG_ADDSELECTION | SELFLAG_REMOVESELECTION)) != 0) {
        try {
            tree_.set_selected(node, (flags & SELFLAG_ADDSELECTION) != 0);
        } catch (...) {
        }
        InvalidateRect(wnd_, nullptr, FALSE);
        acc_event((flags & SELFLAG_ADDSELECTION) != 0 ? EVENT_OBJECT_SELECTIONADD
                                                      : EVENT_OBJECT_SELECTIONREMOVE,
                  static_cast<std::ptrdiff_t>(row));
    }
    if ((flags & SELFLAG_TAKEFOCUS) != 0) focus_row(row);
}

void TreeView::acc_default_action(std::size_t row) noexcept {
    if (row >= tree_.row_count()) return;
    const std::uint32_t node = tree_.node_at_row(row);
    if (selected_row_ != static_cast<std::ptrdiff_t>(row) || tree_.selection_hint() > 1) {
        select_row(row);
    }
    run_gesture(actions::Gesture::enter, node);
}

} // namespace filetree::view
