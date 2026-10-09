#include "tree.h"

#include "filter_rules.h"

#include <windows.h>

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace filetree::model {
namespace {

std::uint16_t clamp16(std::size_t value) noexcept {
    return static_cast<std::uint16_t>(std::min<std::size_t>(value, 0xffff));
}

} // namespace

void Tree::clear() noexcept {
    nodes_.clear();
    rows_.clear();
    previous_rows_.clear();
    names_.clear();
    selected_.clear();
    anchor_ = no_node;
}

std::uint32_t Tree::add_root(std::wstring_view path, std::uint32_t attributes,
                            std::uint16_t flags) {
    const auto index = static_cast<std::uint32_t>(nodes_.size());
    Node& node = nodes_.emplace_back();
    node.name = names_.intern(path);
    node.name_length = clamp16(path.size());
    node.attributes = attributes;
    node.flags = static_cast<std::uint16_t>(node_container | node_root | flags);
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
    if (filtered()) return rebuild_rows();
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
    deselect_descendants(index);
    if (filtered()) return rebuild_rows();

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

void Tree::orphan_children(std::uint32_t index) noexcept {
    const Node& parent = nodes_[index];
    for (std::uint32_t child = parent.first_child;
         parent.child_count != 0 && child < parent.first_child + parent.child_count; ++child) {
        Node& node = nodes_[child];
        node.flags = static_cast<std::uint16_t>(node.flags & ~(node_loading | node_expanded));
        if (node.has(node_loaded)) orphan_children(child);
    }
}

Tree::ReloadResult Tree::reload(std::uint32_t index) {
    ReloadResult result;
    Node& node = nodes_[index];
    if (!node.has(node_container) || node.has(node_loading)) return result;

    if (node.has(node_expanded) && !filtered()) {
        if (const auto row = row_of(index)) {
            const std::uint16_t depth = node.depth;
            std::size_t end = *row + 1;
            while (end < rows_.size() && nodes_[rows_[end]].depth > depth) ++end;
            result.splice = {*row + 1, end - (*row + 1), 0};
            rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(*row + 1),
                        rows_.begin() + static_cast<std::ptrdiff_t>(end));
        }
    }
    if (node.has(node_loaded)) orphan_children(index);
    node.first_child = no_node;
    node.child_count = 0;
    node.flags = static_cast<std::uint16_t>(node.flags & ~(node_loaded | node_load_failed));
    if (node.has(node_expanded)) {
        node.flags |= node_loading;
        result.needs_load = true;
    }
    if (filtered()) result.splice = rebuild_rows();
    return result;
}

bool Tree::children_match(std::uint32_t index,
                          std::span<const ChildRecord> children) const noexcept {
    const Node& node = nodes_[index];
    if (!node.has(node_loaded) || node.child_count != children.size()) return false;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const Node& child = nodes_[node.first_child + i];
        const ChildRecord& record = children[i];
        if (child.attributes != record.attributes || child.size != record.size ||
            child.modified != record.modified || child.name_view() != record.name) {
            return false;
        }
    }
    return true;
}

Tree::MergeResult Tree::merge_children(std::uint32_t index,
                                       std::span<const ChildRecord> children) {
    MergeResult result;
    {
        const Node& node = nodes_[index];
        if (!node.has(node_loaded) || node.has(node_loading)) return result;
    }
    if (nodes_.size() + children.size() >= std::numeric_limits<std::uint32_t>::max()) {
        return result;
    }
    const std::uint32_t old_first = nodes_[index].first_child;
    const std::uint32_t old_count = nodes_[index].child_count;
    result.old_first = old_count != 0 ? old_first : no_node;
    result.moved.assign(old_count, no_node);

    // Remove the visible descendants first (they hold old indices).
    std::optional<std::size_t> row;
    std::size_t removed = 0;
    if (nodes_[index].has(node_expanded) && !filtered()) {
        row = row_of(index);
        if (row) {
            const std::uint16_t depth = nodes_[index].depth;
            std::size_t end = *row + 1;
            while (end < rows_.size() && nodes_[rows_[end]].depth > depth) ++end;
            removed = end - (*row + 1);
            rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(*row + 1),
                        rows_.begin() + static_cast<std::ptrdiff_t>(end));
        }
    }

    // Both listings come sorted the same way, so the next old child is usually the match; a
    // name index over the old children (built on the first miss) covers the rest.
    const auto first = static_cast<std::uint32_t>(nodes_.size());
    const std::uint16_t depth = static_cast<std::uint16_t>(nodes_[index].depth + 1);
    nodes_.reserve(nodes_.size() + children.size());
    std::unordered_map<std::wstring_view, std::uint32_t> by_name;
    std::uint32_t cursor = 0;
    for (const ChildRecord& record : children) {
        const bool container = (record.attributes & 0x10 /* FILE_ATTRIBUTE_DIRECTORY */) != 0;
        std::uint32_t match = no_node;
        if (cursor < old_count && result.moved[cursor] == no_node &&
            nodes_[old_first + cursor].name_view() == record.name) {
            match = cursor;
        } else if (old_count != 0) {
            if (by_name.empty()) {
                by_name.reserve(old_count);
                for (std::uint32_t i = 0; i < old_count; ++i) {
                    by_name.emplace(nodes_[old_first + i].name_view(), i);
                }
            }
            if (const auto found = by_name.find(record.name);
                found != by_name.end() && result.moved[found->second] == no_node) {
                match = found->second;
            }
        }
        if (match != no_node && nodes_[old_first + match].has(node_container) != container) {
            match = no_node; // a file became a folder or the other way round
        }
        const auto at = static_cast<std::uint32_t>(nodes_.size());
        if (match != no_node) {
            cursor = match + 1;
            result.moved[match] = at;
            if (anchor_ == old_first + match) anchor_ = at;
            Node moved = nodes_[old_first + match];
            moved.size = record.size;
            moved.modified = record.modified;
            moved.attributes = record.attributes;
            nodes_.push_back(moved);
            if (moved.has(node_selected)) selected_.push_back(at);
            Node& old = nodes_[old_first + match];
            if (moved.has(node_loaded)) {
                for (std::uint32_t g = moved.first_child; g < moved.first_child + moved.child_count;
                     ++g) {
                    nodes_[g].parent = at;
                }
            }
            old.flags = 0; // orphan: no longer reachable, any listing for it is ignored
            old.first_child = no_node;
            old.child_count = 0;
        } else {
            Node& child = nodes_.emplace_back();
            child.name = names_.intern(record.name);
            child.name_length = clamp16(record.name.size());
            child.size = record.size;
            child.modified = record.modified;
            child.attributes = record.attributes;
            child.parent = index;
            child.depth = depth;
            if (container) child.flags = node_container;
        }
    }
    for (std::uint32_t i = 0; i < old_count; ++i) {
        if (result.moved[i] != no_node) continue;
        if (anchor_ == old_first + i) anchor_ = no_node;
        orphan_children(old_first + i);
        nodes_[old_first + i].flags = 0;
    }

    Node& node = nodes_[index];
    node.first_child = first;
    node.child_count = static_cast<std::uint32_t>(children.size());

    if (filtered()) {
        result.splice = rebuild_rows();
    } else if (row) {
        scratch_.clear();
        append_visible_subtree(index);
        rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(*row + 1), scratch_.begin(),
                     scratch_.end());
        result.splice = {*row + 1, removed, scratch_.size()};
    }
    return result;
}

std::uint32_t Tree::find_child(std::uint32_t parent, std::wstring_view name) const noexcept {
    const Node& node = nodes_[parent];
    if (!node.has(node_loaded)) return no_node;
    for (std::uint32_t child = node.first_child; child < node.first_child + node.child_count;
         ++child) {
        const Node& c = nodes_[child];
        if (CompareStringOrdinal(c.name, c.name_length, name.data(),
                                 static_cast<int>(name.size()), TRUE) == CSTR_EQUAL) {
            return child;
        }
    }
    return no_node;
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

RowSplice Tree::set_filter(std::wstring_view text) {
    wchar_t upper[256];
    const std::size_t length = to_upper(text, upper, std::size(upper));
    std::wstring next(upper, length);
    if (next == filter_) return {};
    filter_ = std::move(next);
    filter_glob_ = filter_.find_first_of(L"*?") != std::wstring::npos;
    return rebuild_rows();
}

bool Tree::matches_filter(const Node& node) noexcept {
    const std::size_t length = to_upper(node.name_view(), upper_, std::size(upper_));
    const std::wstring_view name(upper_, length);
    return filter_glob_ ? glob_match(filter_, name) : name.find(filter_) != std::wstring_view::npos;
}

bool Tree::collect_filtered(std::uint32_t index, bool inside_match) {
    const bool self = inside_match || matches_filter(nodes_[index]);
    const std::size_t mark = rows_.size();
    rows_.push_back(index);
    bool any_child = false;
    const Node& node = nodes_[index];
    if (node.has(node_expanded) && node.has(node_loaded)) {
        const std::uint32_t first = node.first_child;
        const std::uint32_t count = node.child_count;
        for (std::uint32_t child = first; child < first + count; ++child) {
            any_child = collect_filtered(child, self) || any_child;
        }
    }
    if (!self && !any_child) {
        rows_.resize(mark);
        return false;
    }
    return true;
}

RowSplice Tree::collapse_all() {
    for (Node& node : nodes_) {
        node.flags = static_cast<std::uint16_t>(node.flags & ~(node_expanded | node_load_failed));
        if (!node.has(node_root)) node.flags = static_cast<std::uint16_t>(node.flags & ~node_selected);
    }
    return rebuild_rows();
}

RowSplice Tree::rebuild_rows() {
    previous_rows_.swap(rows_);
    rows_.clear();
    // Roots come first in the pool (add_root is only called on a cleared tree).
    for (std::uint32_t root = 0; root < nodes_.size() && nodes_[root].has(node_root); ++root) {
        if (filtered()) {
            collect_filtered(root, false);
        } else {
            rows_.push_back(root);
            const Node& node = nodes_[root];
            if (node.has(node_expanded) && node.has(node_loaded)) {
                scratch_.clear();
                append_visible_subtree(root);
                rows_.insert(rows_.end(), scratch_.begin(), scratch_.end());
            }
        }
    }
    RowSplice splice{0, previous_rows_.size(), rows_.size()};
    splice.full = true;
    return splice;
}

std::size_t Tree::memory_bytes() const noexcept {
    return nodes_.capacity() * sizeof(Node) + rows_.capacity() * sizeof(std::uint32_t) +
           scratch_.capacity() * sizeof(std::uint32_t) + names_.size_chars() * sizeof(wchar_t);
}

} // namespace filetree::model

