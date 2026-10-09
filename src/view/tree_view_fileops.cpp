// New folder (F7), the shell clipboard (Ctrl+X/C/V) and dropping files onto the panel. The disk
// work is the shell's (actions/file_ops.h); here: which folder, and showing the result.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include "../actions/file_ops.h"
#include "../actions/playlist_send.h"

namespace filetree::view {
namespace {

constexpr ULONGLONG drop_open_delay_ms = 800; //!< hover over a closed folder to open it
constexpr ULONGLONG drop_scroll_ms = 60;      //!< one row per step at the edges

} // namespace

std::uint32_t TreeView::target_folder(std::uint32_t node) const noexcept {
    if (node == model::no_node || node >= tree_.node_count()) return model::no_node;
    const model::Node& n = tree_.node(node);
    return n.has(model::node_container) ? node : n.parent;
}

void TreeView::check_if_open(std::uint32_t folder) noexcept {
    if (folder >= tree_.node_count()) return;
    const model::Node& f = tree_.node(folder);
    if (!f.has(model::node_loaded) || f.has(model::node_loading) || !tree_.row_of(folder)) return;
    try {
        request_check(folder);
    } catch (...) {
    }
}

void TreeView::show_properties(std::uint32_t node) noexcept {
    try {
        tree_.build_path(node, path_);
        actions::show_properties(path_, wnd_);
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

void TreeView::save_as_playlist(std::uint32_t folder) noexcept {
    try {
        std::wstring path;
        tree_.build_path(folder, path);
        std::wstring name(model::display_name(tree_.node(folder)));
        while (!name.empty() && (name.back() == L'\\' || name.back() == L':')) name.pop_back();
        if (name.empty()) name = L"Playlist";
        std::wstring file;
        if (actions::pick_playlist_file(wnd_, path, name, file)) {
            actions::save_as_playlist(path, std::move(file), wnd_);
        }
    } catch (...) {
    }
}

void TreeView::new_folder(std::uint32_t node) noexcept {
    const std::uint32_t folder = target_folder(node);
    if (folder == model::no_node) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    try {
        std::wstring path;
        tree_.build_path(folder, path);
        std::weak_ptr<TreeView*> weak = alive_;
        actions::new_folder(
            std::move(path), wnd_,
            [weak, folder, generation = generation_](actions::ShellResult result,
                                                     std::wstring name) {
                const auto alive = weak.lock();
                if (!alive || !result.succeeded || name.empty()) return;
                TreeView& view = **alive;
                if (view.wnd_ == nullptr || view.generation_ != generation ||
                    folder >= view.tree_.node_count()) {
                    return;
                }
                // Open the folder, then select the new one and rename it, as Explorer does.
                if (!view.tree_.node(folder).has(model::node_expanded)) view.expand(folder);
                view.reload_and_select(folder, std::move(name), {}, true);
            });
    } catch (...) {
    }
}

void TreeView::put_on_clipboard(std::uint32_t node, bool cut) noexcept {
    const model::Node& n = tree_.node(node);
    // A drive cannot be moved; a favourite root is a real folder and can.
    if (n.parent == model::no_node && !n.has(model::node_favourite)) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    try {
        tree_.build_path(node, path_);
        if (!actions::set_clipboard_file(path_, cut)) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        set_cut(cut ? node : model::no_node);
    } catch (...) {
    }
}

void TreeView::set_cut(std::uint32_t node) noexcept {
    if (wnd_ == nullptr) return;
    if (cut_node_ != model::no_node && cut_node_ < tree_.node_count()) {
        if (const auto row = tree_.row_of(cut_node_)) invalidate_row(*row);
    }
    cut_node_ = node;
    if (node != model::no_node) {
        cut_sequence_ = GetClipboardSequenceNumber();
        if (const auto row = tree_.row_of(node)) invalidate_row(*row);
        // Told when anything else takes the clipboard (only while a cut is shown).
        if (!clipboard_listening_) clipboard_listening_ = AddClipboardFormatListener(wnd_) != FALSE;
    } else if (clipboard_listening_) {
        RemoveClipboardFormatListener(wnd_);
        clipboard_listening_ = false;
    }
}

void TreeView::on_clipboard_update() noexcept {
    if (cut_node_ != model::no_node && GetClipboardSequenceNumber() != cut_sequence_) {
        set_cut(model::no_node);
    }
}

void TreeView::paste_into(std::uint32_t node) noexcept {
    const std::uint32_t folder = target_folder(node);
    actions::ClipboardFiles files;
    if (folder == model::no_node || !actions::read_clipboard_files(wnd_, files)) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    copy_into(folder, std::move(files.paths), files.cut, files.cut ? files.sequence : 0);
}

void TreeView::copy_into(std::uint32_t folder, std::vector<std::wstring> paths, bool move,
                         DWORD clipboard_sequence) noexcept {
    try {
        std::wstring target;
        tree_.build_path(folder, target);
        actions::copy_items(
            std::move(paths), std::move(target), move, wnd_,
            guard([folder, move, clipboard_sequence](TreeView& view, actions::ShellResult result) {
                if (!result.ran) return;
                // A pasted cut is used up, as in Explorer.
                if (move && result.succeeded && clipboard_sequence != 0) {
                    actions::clear_clipboard_if(view.wnd_, clipboard_sequence);
                }
                // Change watching would catch it too, but may be off or not watching this folder.
                view.check_if_open(folder);
            }));
    } catch (...) {
    }
}

void TreeView::set_drop_row(std::ptrdiff_t row) noexcept {
    if (row == drop_row_) return;
    if (drop_row_ >= 0 && static_cast<std::size_t>(drop_row_) < tree_.row_count()) {
        invalidate_row(static_cast<std::size_t>(drop_row_));
    }
    drop_row_ = row;
    if (row >= 0) invalidate_row(static_cast<std::size_t>(row));
}

DWORD TreeView::drag_over(IDataObject* data, DWORD keys, POINT point, DWORD allowed,
                          bool enter) noexcept {
    if (enter) {
        drop_files_ = actions::has_files(data);
        drop_hover_node_ = model::no_node;
    }
    if (!drop_files_ || wnd_ == nullptr || edit_ != nullptr) {
        set_drop_row(-1);
        return DROPEFFECT_NONE;
    }

    // Scroll while held near the top or bottom edge.
    const ULONGLONG now = GetTickCount64();
    const int edge = metrics_.row_height;
    if ((point.y < edge || point.y > client_height_ - edge) && now - drop_scrolled_at_ >= drop_scroll_ms) {
        drop_scrolled_at_ = now;
        if (point.y < edge && top_row_ > 0) {
            scroll_to(top_row_ - 1);
        } else if (point.y > client_height_ - edge && top_row_ < max_top_row()) {
            scroll_to(top_row_ + 1);
        }
    }

    const std::ptrdiff_t row = row_at(point.y);
    const std::uint32_t node =
        row >= 0 ? tree_.node_at_row(static_cast<std::size_t>(row)) : model::no_node;
    const std::uint32_t folder = target_folder(node);
    const auto folder_row = folder != model::no_node ? tree_.row_of(folder) : std::nullopt;
    if (!folder_row) {
        set_drop_row(-1);
        drop_hover_node_ = model::no_node;
        return DROPEFFECT_NONE;
    }
    set_drop_row(static_cast<std::ptrdiff_t>(*folder_row));

    // Hovering over a closed folder opens it.
    if (node == folder && !tree_.node(folder).has(model::node_expanded)) {
        if (drop_hover_node_ != folder) {
            drop_hover_node_ = folder;
            drop_hover_since_ = now;
        } else if (now - drop_hover_since_ >= drop_open_delay_ms) {
            drop_hover_node_ = model::no_node;
            expand(folder);
        }
    } else {
        drop_hover_node_ = model::no_node;
    }

    // Copy by default (as the preferred effect of our own drags), Shift moves.
    const bool move = (keys & MK_SHIFT) != 0 && (keys & MK_CONTROL) == 0;
    if (move && (allowed & DROPEFFECT_MOVE) != 0) return DROPEFFECT_MOVE;
    if ((allowed & DROPEFFECT_COPY) != 0) return DROPEFFECT_COPY;
    return (allowed & DROPEFFECT_MOVE) != 0 ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
}

void TreeView::drag_leave() noexcept {
    set_drop_row(-1);
    drop_hover_node_ = model::no_node;
}

DWORD TreeView::drop(IDataObject* data, DWORD keys, POINT point, DWORD allowed) noexcept {
    // drag_over does the hit test and picks the effect (no hover-open on the release itself).
    drop_hover_node_ = model::no_node;
    const DWORD effect = drag_over(data, keys, point, allowed, false);
    const std::ptrdiff_t row = drop_row_;
    set_drop_row(-1);
    drop_hover_node_ = model::no_node;
    if (effect == DROPEFFECT_NONE || row < 0) return DROPEFFECT_NONE;
    try {
        std::vector<std::wstring> paths;
        if (!actions::read_files(data, paths)) return DROPEFFECT_NONE;
        const std::uint32_t folder = tree_.node_at_row(static_cast<std::size_t>(row));
        const bool move = effect == DROPEFFECT_MOVE;
        copy_into(folder, std::move(paths), move, 0);
        if (!move) return DROPEFFECT_COPY;
        // We move the files ourselves (on a worker): the source must not delete anything.
        actions::report_optimized_move(data);
        return DROPEFFECT_NONE;
    } catch (...) {
    }
    return DROPEFFECT_NONE;
}

} // namespace filetree::view
