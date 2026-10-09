// Tree selection: the node_selected flag plus a list of flagged nodes, so clearing does not
// walk the whole pool. The list may hold stale entries (nodes orphaned by a merge or reload,
// or deselected one by one); every use checks the flag.

#include "tree.h"

#include <algorithm>

namespace filetree::model {

void Tree::set_selected(std::uint32_t index, bool selected) {
    Node& node = nodes_[index];
    if (node.has(node_selected) == selected) return;
    if (selected) {
        selected_.push_back(index);
        node.flags |= node_selected;
    } else {
        node.flags = static_cast<std::uint16_t>(node.flags & ~node_selected);
        if (!selected_.empty() && selected_.back() == index) selected_.pop_back();
    }
}

std::size_t Tree::clear_selection() noexcept {
    std::size_t cleared = 0;
    for (const std::uint32_t index : selected_) {
        if (index >= nodes_.size()) continue;
        Node& node = nodes_[index];
        if (!node.has(node_selected)) continue;
        node.flags = static_cast<std::uint16_t>(node.flags & ~node_selected);
        ++cleared;
    }
    selected_.clear();
    return cleared;
}

void Tree::select_rows(std::size_t first, std::size_t last) {
    if (rows_.empty()) return;
    if (first > last) std::swap(first, last);
    last = std::min(last, rows_.size() - 1);
    for (std::size_t row = first; row <= last; ++row) set_selected(rows_[row], true);
}

void Tree::selected_nodes(std::vector<std::uint32_t>& out) const {
    out.clear();
    if (selected_.empty()) return;
    for (const std::uint32_t index : rows_) {
        if (nodes_[index].has(node_selected)) out.push_back(index);
    }
}

std::size_t Tree::count_selected_rows(std::size_t limit) const noexcept {
    std::size_t count = 0;
    if (selected_.empty() || limit == 0) return 0;
    for (const std::uint32_t index : rows_) {
        if (nodes_[index].has(node_selected) && ++count >= limit) break;
    }
    return count;
}

void Tree::deselect_descendants(std::uint32_t index) noexcept {
    for (const std::uint32_t selected : selected_) {
        if (selected >= nodes_.size() || !nodes_[selected].has(node_selected)) continue;
        for (std::uint32_t up = nodes_[selected].parent; up != no_node; up = nodes_[up].parent) {
            if (up == index) {
                Node& node = nodes_[selected];
                node.flags = static_cast<std::uint16_t>(node.flags & ~node_selected);
                break;
            }
        }
    }
}

} // namespace filetree::model
