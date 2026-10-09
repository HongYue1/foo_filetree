#pragma once

// The folder tree: a node pool plus the flat list of visible rows. Main thread only.
//
// - Nodes live in one vector and are addressed by 32-bit index. A folder's children are one
//   contiguous index range, appended when its listing arrives (already sorted by the worker).
// - Visible rows are a flat vector of node indices in display order. Expanding splices the
//   folder's visible subtree in after it; collapsing erases the rows below it that are deeper.
//   Paint and hit-test (M2) index this vector directly.
// - Collapsing keeps the children, so re-expanding is instant. Memory therefore grows with what
//   has been expanded this session; unloading large collapsed subtrees is an M7 item.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "name_pool.h"

namespace filetree::model {

inline constexpr std::uint32_t no_node = 0xffffffffu;

enum NodeFlag : std::uint16_t {
    node_container = 1 << 0,   //!< folder or root: can be expanded
    node_expanded = 1 << 1,    //!< the user wants it open (rows shown once loaded)
    node_loaded = 1 << 2,      //!< children are present (possibly zero of them)
    node_loading = 1 << 3,     //!< a listing has been requested and not yet applied
    node_load_failed = 1 << 4, //!< the last listing failed (access denied, offline, ...)
    node_root = 1 << 5,
    node_favourite = 1 << 6, //!< a root from the favourites list (else a drive)
    node_selected = 1 << 7,  //!< in the multi-selection (follows the node through merges)
};

struct Node {
    const wchar_t* name{};        //!< NamePool, null-terminated
    std::uint64_t size{};
    std::int64_t modified{};      //!< FILETIME
    std::uint32_t parent{no_node};
    std::uint32_t first_child{no_node};
    std::uint32_t child_count{};
    std::uint32_t attributes{};
    std::uint16_t name_length{};
    std::uint16_t depth{};
    std::uint16_t flags{};
    std::uint16_t reserved{};

    [[nodiscard]] bool has(NodeFlag flag) const noexcept { return (flags & flag) != 0; }
    [[nodiscard]] std::wstring_view name_view() const noexcept { return {name, name_length}; }
};

// Budget: <= 64 bytes per node plus the name.
static_assert(sizeof(Node) <= 64, "Node exceeds the per-node memory budget");

//! The name as shown: a drive root "C:\\" as "C:", a favourite root by its last component.
[[nodiscard]] inline std::wstring_view display_name(const Node& node) noexcept {
    std::wstring_view name = node.name_view();
    if (!node.has(node_root)) return name;
    if (node.has(node_favourite)) {
        while (name.size() > 1 && name.back() == L'\\') name.remove_suffix(1);
        const std::size_t slash = name.find_last_of(L'\\');
        if (slash != std::wstring_view::npos && slash + 1 < name.size()) name.remove_prefix(slash + 1);
        return name;
    }
    if (name.size() == 3 && name[1] == L':') name.remove_suffix(1);
    return name;
}

//! What changed in the row list, for the view to invalidate and fix scroll/selection.
//! `row` is the first affected row; rows [row, row + removed) were replaced by `inserted` rows.
struct RowSplice {
    std::size_t row{};
    std::size_t removed{};
    std::size_t inserted{};
    //! Every row was rebuilt (a name filter is active): map rows through previous_node_at().
    bool full{false};

    [[nodiscard]] bool empty() const noexcept { return removed == 0 && inserted == 0; }
};

//! One child as delivered by a listing, in display order.
struct ChildRecord {
    std::wstring_view name;
    std::uint32_t attributes{};
    std::uint64_t size{};
    std::int64_t modified{};
};

class Tree {
public:
    enum class ExpandResult {
        not_container, //!< files cannot expand
        unchanged,     //!< already expanded
        expanded,      //!< rows inserted (see the splice)
        needs_load,    //!< marked expanded + loading; request a listing, then apply_children()
        loading,       //!< marked expanded; a listing is already on its way
    };

    void clear() noexcept;

    //! Adds a top-level entry (a drive "C:\", a favourite folder) as a new last row.
    std::uint32_t add_root(std::wstring_view path, std::uint32_t attributes = 0x10,
                           std::uint16_t flags = 0);

    [[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
    [[nodiscard]] std::uint32_t node_at_row(std::size_t row) const noexcept { return rows_[row]; }
    [[nodiscard]] std::span<const std::uint32_t> rows() const noexcept { return rows_; }
    [[nodiscard]] const Node& node(std::uint32_t index) const noexcept { return nodes_[index]; }
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }

    //! The node's row, if it is visible. Linear in the row count (fine at 10k rows: one pass
    //! over contiguous integers); called once per expand/collapse/listing, never per paint.
    [[nodiscard]] std::optional<std::size_t> row_of(std::uint32_t index) const noexcept;

    ExpandResult expand(std::uint32_t index, RowSplice* splice = nullptr);
    //! Returns the removed rows; empty if it was not expanded or not visible.
    RowSplice collapse(std::uint32_t index);

    //! Applies a finished listing. Ignored unless the node is still waiting for one (a refresh
    //! that races a collapse is harmless). Splices rows in if the node is expanded and visible.
    RowSplice apply_children(std::uint32_t index, std::span<const ChildRecord> children);

    //! Marks a listing as failed: not loading, not loaded, flagged. The node stays expanded with
    //! no children, so the view can show the error state; expanding it again retries.
    void fail_load(std::uint32_t index) noexcept;

    struct ReloadResult {
        RowSplice splice;        //!< the node's visible descendants, removed
        bool needs_load{false};  //!< expanded: marked loading, request a listing
    };

    //! Forgets a folder's children so they are listed again (Refresh, after rename/delete).
    //! Visible descendants are removed; the old child nodes stay in the pool as orphans (no
    //! longer reachable; their pending listings are ignored because loading is cleared). If the
    //! folder is expanded it is marked loading; if it is already loading nothing changes.
    ReloadResult reload(std::uint32_t index);

    //! Soft refresh: true when a fresh listing has exactly the loaded children (same order,
    //! names, attributes, sizes and times). False if the folder is not loaded.
    [[nodiscard]] bool children_match(std::uint32_t index,
                                      std::span<const ChildRecord> children) const noexcept;

    struct MergeResult {
        RowSplice splice;                 //!< the folder's visible descendants, replaced
        std::uint32_t old_first{no_node}; //!< the old child range...
        std::vector<std::uint32_t> moved; //!< ...and where each went (no_node: gone)
        [[nodiscard]] std::uint32_t map(std::uint32_t old) const noexcept {
            if (old_first == no_node || old < old_first || old - old_first >= moved.size()) {
                return old;
            }
            return moved[old - old_first];
        }
    };

    //! Replaces a loaded folder's children with a fresh listing in one step. Children whose
    //! name (exact) and kind are unchanged keep their state and subtree (open folders stay
    //! open, nothing is listed again) but move to a new index: translate held indices with
    //! MergeResult::map. Gone children become orphans. No-op unless loaded and not loading.
    MergeResult merge_children(std::uint32_t index, std::span<const ChildRecord> children);

    //! The child of `parent` with this name (case-insensitive, as NTFS), or no_node. Linear.
    [[nodiscard]] std::uint32_t find_child(std::uint32_t parent,
                                           std::wstring_view name) const noexcept;

    //! Full path of a node: the root's path plus each component, '\' separated.
    void build_path(std::uint32_t index, std::wstring& out) const;

    //! Name filter. Empty shows every expanded row; otherwise a row shows when its name contains
    //! the text (or matches it as a whole, with * and ?), when it is inside a matching folder, or
    //! when a shown row is below it. Applies to listed, expanded folders only. While a filter is
    //! active, row changes rebuild the whole list (RowSplice::full).
    RowSplice set_filter(std::wstring_view text);
    [[nodiscard]] bool filtered() const noexcept { return !filter_.empty(); }
    //! Closes every folder (children stay listed). Returns a full rebuild.
    RowSplice collapse_all();
    //! Rebuilds the rows from the expanded state (after the roots were re-added).
    RowSplice rebuild_rows();
    //! The node a row showed before the last full rebuild, or no_node.
    [[nodiscard]] std::uint32_t previous_node_at(std::size_t row) const noexcept {
        return row < previous_rows_.size() ? previous_rows_[row] : no_node;
    }

    // Selection (tree_selection.cpp): a flag on the nodes, so it follows them through merges,
    // splices and filter rebuilds. Collapsing a folder deselects what it hides; a filter keeps
    // hidden rows selected (they count again when they reappear).
    void set_selected(std::uint32_t index, bool selected);
    [[nodiscard]] bool is_selected(std::uint32_t index) const noexcept {
        return nodes_[index].has(node_selected);
    }
    //! Returns how many nodes were deselected.
    std::size_t clear_selection() noexcept;
    //! Adds rows [first, last] (either order) to the selection.
    void select_rows(std::size_t first, std::size_t last);
    //! Selected nodes on visible rows, in row order.
    void selected_nodes(std::vector<std::uint32_t>& out) const;
    //! Selected visible rows, counted up to `limit` (cheap "one or several" tests).
    [[nodiscard]] std::size_t count_selected_rows(std::size_t limit = SIZE_MAX) const noexcept;
    //! At least this many nodes may be selected (an upper bound; 0 means none).
    [[nodiscard]] std::size_t selection_hint() const noexcept { return selected_.size(); }
    //! Deselects every selected node below `index`.
    void deselect_descendants(std::uint32_t index) noexcept;
    //! Shift+click / Shift+arrow ranges start here; follows merges like the selection.
    void set_anchor(std::uint32_t index) noexcept { anchor_ = index; }
    [[nodiscard]] std::uint32_t anchor() const noexcept { return anchor_; }

    //! Bytes held by nodes, rows and names. For the performance counters (M7).
    [[nodiscard]] std::size_t memory_bytes() const noexcept;

private:
    void append_visible_subtree(std::uint32_t index);
    RowSplice splice_children_in(std::uint32_t index);
    void orphan_children(std::uint32_t index) noexcept;
    [[nodiscard]] bool matches_filter(const Node& node) noexcept;
    bool collect_filtered(std::uint32_t index, bool inside_match);

    std::vector<Node> nodes_;
    std::vector<std::uint32_t> rows_;
    std::vector<std::uint32_t> scratch_; //!< reused by expand; grows, never shrinks
    std::vector<std::uint32_t> previous_rows_;
    std::wstring filter_;                //!< upper-cased
    bool filter_glob_{false};
    wchar_t upper_[256]{};               //!< a name upper-cased for matching
    NamePool names_;
    std::vector<std::uint32_t> selected_; //!< every flagged node (may hold stale entries)
    std::uint32_t anchor_{no_node};
};

} // namespace filetree::model

