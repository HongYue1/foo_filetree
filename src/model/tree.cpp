#include "tree.h"

#include <algorithm>
#include <limits>

namespace filetree::model {
namespace {

std::uint16_t clamp16(std::size_t value) noexcept {
    return static_cast<std::uint16_t>(std::min<std::size_t>(value, 0xffff));
}

} // namespace

void Tree::clear() noexcept {
    nodes_.clear();
    rows_.clear();
    names_.clear();
}

std::uint32_t Tree::add_root(std::wstring_view path, std::uint32_t attributes) {
    const auto index = static_cast<std::uint32_t>(nodes_.size());
    Node& node = nodes_.emplace_back();
    node.name = names_.intern(path);
    node.name_length = clamp16(path.size());
    node.attributes = attributes;
    node.flags = node_container | node_root;
    rows_.push_back(index);
    return index;
}

std::optional<std::size_t> Tree::row_of(std::uint32_t index) const noexcept {
    const auto found = std::find(rows_.begin(), rows_.end(), index);
    if (found == rows_.end()) return std::nullopt;
    return static_cast<std::size_t>(found - rows_.begin());
}

void Tree::append_visible_subtree(std::uint32_t index) {
    const Node& parent = nodes_[index];
    const std::uint32_t first = parent.first_child;
    const std::uint32_t count = parent.child_count;
    for (std::uint32_t child = first; child < first + count; ++child) {
        scratch_.push_back(child);
        const Node& node = nodes_[child];
        if (node.has(node_expanded) && node.has(node_loaded)) append_visible_subtree(child);
    }
}

RowSplice Tree::splice_children_in(std::uint32_t index) {
    const auto row = row_of(index);
    if (!row) return {};

    scratch_.clear();
    append_visible_subtree(index);
    rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(*row + 1), scratch_.begin(),
                 scratch_.end());
    return {*row + 1, 0, scratch_.size()};
}

Tree::ExpandResult Tree::expand(std::uint32_t index, RowSplice* splice) {
    Node& node = nodes_[index];
    if (!node.has(node_container)) return ExpandResult::not_container;
    if (node.has(node_expanded) && !node.has(node_load_failed)) return ExpandResult::unchanged;

    node.flags |= node_expanded;
    if (node.has(node_loading)) return ExpandResult::loading;
    if (!node.has(node_loaded)) {
        node.flags = static_cast<std::uint16_t>((node.flags | node_loading) & ~node_load_failed);
        return ExpandResult::needs_load;
    }

    const RowSplice result = splice_children_in(index);
    if (splice != nullptr) *splice = result;
    return ExpandResult::expanded;
}

RowSplice Tree::collapse(std::uint32_t index) {
    Node& node = nodes_[index];
    if (!node.has(node_expanded)) return {};
    node.flags = static_cast<std::uint16_t>(node.flags & ~(node_expanded | node_load_failed));

    const auto row = row_of(index);
    if (!row) return {};

    const std::uint16_t depth = node.depth;
    std::size_t end = *row + 1;
    while (end < rows_.size() && nodes_[rows_[end]].depth > depth) ++end;

    const std::size_t removed = end - (*row + 1);
    rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(*row + 1),
                rows_.begin() + static_cast<std::ptrdiff_t>(end));
    return {*row + 1, removed, 0};
}

RowSplice Tree::apply_children(std::uint32_t index, std::span<const ChildRecord> children) {
    if (!nodes_[index].has(node_loading)) return {};
    if (nodes_.size() + children.size() >= std::numeric_limits<std::uint32_t>::max()) {
        fail_load(index);
        return {};
    }

    const auto first = static_cast<std::uint32_t>(nodes_.size());
    const std::uint16_t depth = static_cast<std::uint16_t>(nodes_[index].depth + 1);
    nodes_.reserve(nodes_.size() + children.size());
    for (const ChildRecord& record : children) {
        Node& child = nodes_.emplace_back();
        child.name = names_.intern(record.name);
        child.name_length = clamp16(record.name.size());
        child.size = record.size;
        child.modified = record.modified;
        child.attributes = record.attributes;
        child.parent = index;
        child.depth = depth;
        if ((record.attributes & 0x10 /* FILE_ATTRIBUTE_DIRECTORY */) != 0) {
            child.flags = node_container;
        }
    }

    // nodes_ may have reallocated: take the reference only now.
    Node& node = nodes_[index];
    node.first_child = first;
    node.child_count = static_cast<std::uint32_t>(children.size());
    node.flags = static_cast<std::uint16_t>((node.flags | node_loaded) &
                                            ~(node_loading | node_load_failed));

    if (!node.has(node_expanded)) return {};
    return splice_children_in(index);
}

void Tree::fail_load(std::uint32_t index) noexcept {
    Node& node = nodes_[index];
    node.flags = static_cast<std::uint16_t>((node.flags | node_load_failed) &
                                            ~(node_loading | node_loaded));
}

void Tree::build_path(std::uint32_t index, std::wstring& out) const {
    // Collect the chain bottom-up without allocating: depth bounds it.
    std::uint32_t chain[256];
    std::size_t count = 0;
    std::size_t length = 0;
    for (std::uint32_t walk = index; walk != no_node && count < std::size(chain);
         walk = nodes_[walk].parent) {
        chain[count++] = walk;
        length += nodes_[walk].name_length + 1u;
    }

    out.clear();
    out.reserve(length);
    for (std::size_t i = count; i-- > 0;) {
        const Node& node = nodes_[chain[i]];
        if (!out.empty() && out.back() != L'\\') out.push_back(L'\\');
        out.append(node.name, node.name_length);
    }
}

std::size_t Tree::memory_bytes() const noexcept {
    return nodes_.capacity() * sizeof(Node) + rows_.capacity() * sizeof(std::uint32_t) +
           scratch_.capacity() * sizeof(std::uint32_t) + names_.size_chars() * sizeof(wchar_t);
}

} // namespace filetree::model
