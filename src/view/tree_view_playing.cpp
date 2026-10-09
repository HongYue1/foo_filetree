// The now playing marker and the main-menu commands' entry point.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

namespace filetree::view {

void TreeView::on_now_playing_changed() noexcept {
    // Follow: open the folders down to the new track and select it (not while renaming).
    if (follow_playing_ && edit_ == nullptr && !now_playing::path().empty()) {
        navigate_to(now_playing::path(), false);
    }
    resolve_playing();
}

void TreeView::resolve_playing() noexcept {
    std::array<std::uint32_t, 4> nodes{};
    std::size_t count = 0;
    bool exact = false;
    const std::wstring& path = now_playing::path();
    if (mark_playing_ && !path.empty() && wnd_ != nullptr) {
        // Roots are the first nodes. Under each root that holds the path, walk down through open
        // folders only: the walk ends on a visible row.
        for (std::uint32_t root = 0; root < tree_.node_count() &&
                                     tree_.node(root).has(model::node_root) && count < nodes.size();
             ++root) {
            const model::Node& r = tree_.node(root);
            std::wstring_view root_path = r.name_view();
            while (root_path.size() > 3 && root_path.back() == L'\\') root_path.remove_suffix(1);
            if (path.size() <= root_path.size() ||
                CompareStringOrdinal(path.data(), static_cast<int>(root_path.size()),
                                     root_path.data(), static_cast<int>(root_path.size()),
                                     TRUE) != CSTR_EQUAL) {
                continue;
            }
            std::size_t at = root_path.size();
            if (root_path.back() != L'\\') {
                if (path[at] != L'\\') continue; // "D:\Music" must not match "D:\Musicals"
                ++at;
            }
            std::uint32_t node = root;
            bool found_file = false;
            while (at < path.size()) {
                const model::Node& n = tree_.node(node);
                if (!n.has(model::node_expanded) || !n.has(model::node_loaded) ||
                    n.has(model::node_loading)) {
                    break;
                }
                std::size_t end = path.find(L'\\', at);
                if (end == std::wstring::npos) end = path.size();
                const std::uint32_t child =
                    tree_.find_child(node, std::wstring_view(path).substr(at, end - at));
                if (child == model::no_node) break;
                node = child;
                at = end + 1;
                found_file = end == path.size();
            }
            if (tree_.filtered() && !tree_.row_of(node)) continue; // filtered out
            if (found_file && !exact) count = 0; // the file itself beats folders elsewhere
            if (found_file || !exact) {
                nodes[count++] = node;
                exact = exact || found_file;
            }
        }
    }
    if (count == playing_count_ && exact == playing_exact_ &&
        std::equal(nodes.begin(), nodes.begin() + static_cast<std::ptrdiff_t>(count),
                   playing_nodes_.begin())) {
        return;
    }
    // Repaint the old rows and the new ones.
    const auto repaint = [this](std::uint32_t node) {
        if (node < tree_.node_count()) {
            if (const auto row = tree_.row_of(node)) invalidate_row(*row);
        }
    };
    for (std::size_t i = 0; i < playing_count_; ++i) repaint(playing_nodes_[i]);
    playing_nodes_ = nodes;
    playing_count_ = count;
    playing_exact_ = exact;
    for (std::size_t i = 0; i < playing_count_; ++i) repaint(playing_nodes_[i]);
}

int TreeView::playing_mark(std::uint32_t index) const noexcept {
    for (std::size_t i = 0; i < playing_count_; ++i) {
        if (playing_nodes_[i] == index) return playing_exact_ ? 2 : 1;
    }
    return 0;
}

void TreeView::run_command(Command command) noexcept {
    if (wnd_ == nullptr) return;
    const std::uint32_t selected =
        selected_row_ >= 0 ? tree_.node_at_row(static_cast<std::size_t>(selected_row_))
                           : model::no_node;
    switch (command) {
    case Command::show_playing:
        if (now_playing::path().empty() || !navigate_to(now_playing::path(), false)) {
            MessageBeep(MB_ICONWARNING);
        }
        break;
    case Command::refresh: refresh_open_folders(); break;
    case Command::collapse_all: collapse_all(); break;
    case Command::new_folder:
        if (selected == model::no_node) {
            MessageBeep(MB_ICONWARNING);
        } else {
            new_folder(selected);
        }
        break;
    }
}

} // namespace filetree::view
