// TreeView selection: focus row, multi-selection (node_selected in the tree) and the range
// anchor. Explorer's rules: click selects one, Ctrl toggles, Shift selects from the anchor.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

namespace filetree::view {

void TreeView::select_row(std::size_t row) noexcept {
    if (row >= tree_.row_count()) return;
    const std::uint32_t node = tree_.node_at_row(row);
    const bool focus_was_selected =
        selected_row_ >= 0 && tree_.is_selected(tree_.node_at_row(static_cast<std::size_t>(selected_row_)));
    if (selected_row_ >= 0) invalidate_row(static_cast<std::size_t>(selected_row_));
    const bool changed = selected_row_ != static_cast<std::ptrdiff_t>(row);
    filter_hidden_selection_ = model::no_node;
    // One selected row that was the focus row is repainted above; anything else repaints all.
    const std::size_t cleared = tree_.clear_selection();
    if (cleared > 1 || (cleared == 1 && !focus_was_selected)) InvalidateRect(wnd_, nullptr, FALSE);
    try {
        tree_.set_selected(node, true);
    } catch (...) {
    }
    tree_.set_anchor(node);
    selected_row_ = static_cast<std::ptrdiff_t>(row);
    invalidate_row(row);
    ensure_visible(row);
    if (changed) notify_selection();
}

void TreeView::focus_row(std::size_t row) noexcept {
    if (row >= tree_.row_count()) return;
    if (selected_row_ >= 0) invalidate_row(static_cast<std::size_t>(selected_row_));
    const bool changed = selected_row_ != static_cast<std::ptrdiff_t>(row);
    filter_hidden_selection_ = model::no_node;
    selected_row_ = static_cast<std::ptrdiff_t>(row);
    invalidate_row(row);
    ensure_visible(row);
    if (changed) notify_selection();
}

void TreeView::toggle_row(std::size_t row) noexcept {
    if (row >= tree_.row_count()) return;
    const std::uint32_t node = tree_.node_at_row(row);
    const std::size_t before = tree_.selection_hint();
    try {
        tree_.set_selected(node, !tree_.is_selected(node));
    } catch (...) {
    }
    tree_.set_anchor(node);
    acc_event(tree_.is_selected(node) ? EVENT_OBJECT_SELECTIONADD : EVENT_OBJECT_SELECTIONREMOVE,
              static_cast<std::ptrdiff_t>(row));
    // Going from one to several selected rows (or back) adds or drops the focus frame.
    if ((before > 1) != (tree_.selection_hint() > 1) && selected_row_ >= 0) {
        invalidate_row(static_cast<std::size_t>(selected_row_));
    }
    invalidate_row(row);
    focus_row(row);
}

void TreeView::extend_to(std::size_t row, bool add) noexcept {
    const std::size_t rows = tree_.row_count();
    if (row >= rows) return;
    std::size_t anchor = row;
    if (const std::uint32_t node = tree_.anchor(); node != model::no_node) {
        if (const auto found = tree_.row_of(node)) anchor = *found;
    } else if (selected_row_ >= 0 && static_cast<std::size_t>(selected_row_) < rows) {
        anchor = static_cast<std::size_t>(selected_row_);
    }
    const std::uint32_t anchor_node = tree_.node_at_row(anchor);
    if (!add) tree_.clear_selection();
    try {
        tree_.select_rows(anchor, row);
    } catch (...) {
    }
    tree_.set_anchor(anchor_node); // the anchor stays put while Shift is held
    InvalidateRect(wnd_, nullptr, FALSE);
    focus_row(row);
}

void TreeView::move_to(std::size_t row, bool shift, bool ctrl) noexcept {
    keyboard_cue_ = true;
    if (shift) {
        extend_to(row, ctrl);
    } else if (ctrl) {
        focus_row(row);
    } else {
        select_row(row);
    }
}

void TreeView::select_all() noexcept {
    const std::size_t rows = tree_.row_count();
    if (rows == 0) return;
    try {
        tree_.select_rows(0, rows - 1);
    } catch (...) {
    }
    InvalidateRect(wnd_, nullptr, FALSE);
    if (selected_row_ < 0) focus_row(0);
}

void TreeView::selection_or_focus(std::vector<std::uint32_t>& out) const {
    tree_.selected_nodes(out);
    if (out.empty() && selected_row_ >= 0 &&
        static_cast<std::size_t>(selected_row_) < tree_.row_count()) {
        out.push_back(tree_.node_at_row(static_cast<std::size_t>(selected_row_)));
    }
}

} // namespace filetree::view
