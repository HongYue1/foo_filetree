// TreeView context menu and the Explorer-style commands behind it (open, copy path, delete,
// refresh). Rename's inline editor is in inline_edit.cpp.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <windowsx.h>

#include "../actions/fb2k_menu.h"
#include "../actions/shell_menu.h"
#include "../actions/shell_ops.h"

namespace filetree::view {
namespace {

enum MenuId : UINT {
    id_play = 1,
    id_add_active,
    id_new_playlist,
    id_open_explorer,
    id_copy_path,
    id_rename,
    id_delete,
    id_refresh,
};

} // namespace

//! The submenus that need messages forwarded while the popup is tracked.
struct TreeView::MenuSession {
    actions::Fb2kMenu fb2k;
    actions::ShellMenu shell;
};

void TreeView::on_context_menu(LPARAM lp) noexcept {
    if (wnd_ == nullptr) return;
    if (edit_ != nullptr) end_rename(false);
    POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    std::ptrdiff_t row = -1;
    if (point.x == -1 && point.y == -1) {
        // Keyboard (Apps key, Shift+F10): at the selected row.
        if (selected_row_ < 0) return;
        row = selected_row_;
        ensure_visible(static_cast<std::size_t>(row));
        const model::Node& n = tree_.node(tree_.node_at_row(static_cast<std::size_t>(row)));
        point = {text_left(n.depth),
                 static_cast<int>(static_cast<std::size_t>(row) - top_row_ + 1) *
                     metrics_.row_height};
        ClientToScreen(wnd_, &point);
    } else {
        POINT client = point;
        ScreenToClient(wnd_, &client);
        row = row_at(client.y);
        if (row < 0) return;
        if (GetFocus() != wnd_) SetFocus(wnd_);
        select_row(static_cast<std::size_t>(row));
    }

    const std::uint32_t node = tree_.node_at_row(static_cast<std::size_t>(row));
    const model::Node& n = tree_.node(node);
    const bool folder = n.has(model::node_container);
    const bool root = n.has(model::node_root);

    try {
        std::wstring path;
        tree_.build_path(node, path);

        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) return;
        AppendMenuW(menu, MF_STRING, id_play, L"Play");
        AppendMenuW(menu, MF_STRING, id_add_active, L"Add to active playlist");
        AppendMenuW(menu, MF_STRING, id_new_playlist, L"Send to new playlist");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, id_open_explorer,
                    folder ? L"Open in Explorer" : L"Show in folder");
        AppendMenuW(menu, MF_STRING, id_copy_path, L"Copy path\tCtrl+C");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const UINT root_flags = root ? MF_GRAYED : 0u;
        AppendMenuW(menu, MF_STRING | root_flags, id_rename, L"Rename\tF2");
        AppendMenuW(menu, MF_STRING | root_flags, id_delete, L"Delete\tDel");
        AppendMenuW(menu, MF_STRING, id_refresh, L"Refresh\tF5");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        MenuSession session;
        if (!folder) {
            HMENU fb2k = CreatePopupMenu();
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fb2k), L"foobar2000");
            session.fb2k.prepare(fb2k, path);
        }
        HMENU shell = CreatePopupMenu();
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(shell), L"Explorer");
        session.shell.prepare(shell, path, wnd_, GetKeyState(VK_SHIFT) < 0);

        menu_ = &session;
        const UINT id = static_cast<UINT>(TrackPopupMenu(
            menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, wnd_, nullptr));
        menu_ = nullptr;

        if (actions::Fb2kMenu::owns(id)) {
            session.fb2k.invoke(id);
        } else if (actions::ShellMenu::owns(id)) {
            session.shell.invoke(id, point);
        } else if (id != 0) {
            run_menu_command(id, node);
        }
        session.shell.reset();
        DestroyMenu(menu); // destroys the submenus too
    } catch (...) {
        menu_ = nullptr;
    }
}

bool TreeView::forward_menu_message(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept {
    if (menu_ == nullptr) return false;
    if (menu_->fb2k.handle_message(msg, wp, lp, result)) return true;
    return menu_->shell.handle_message(msg, wp, lp, result);
}

void TreeView::run_menu_command(UINT id, std::uint32_t node) noexcept {
    // The tree may have changed while the menu was open (a listing arrived): the node index is
    // still valid (nodes are never freed during a generation), only its row may differ.
    actions::Action action;
    action.kind = actions::Kind::send;
    switch (id) {
    case id_play:
        action.play = true;
        send_node(action, node);
        break;
    case id_add_active:
        action.target = actions::Target::active;
        action.mode = actions::Mode::add;
        send_node(action, node);
        break;
    case id_new_playlist:
        action.target = actions::Target::new_playlist;
        send_node(action, node);
        break;
    case id_open_explorer: open_in_explorer(node); break;
    case id_copy_path: copy_path(node); break;
    case id_rename: begin_rename(node); break;
    case id_delete: delete_node(node, GetKeyState(VK_SHIFT) < 0); break;
    case id_refresh: refresh_node(node); break;
    default: break;
    }
}

std::function<void(bool)> TreeView::guard(std::function<void(TreeView&, bool)> work) {
    std::weak_ptr<TreeView*> weak = alive_;
    return [weak, generation = generation_, work = std::move(work)](bool changed) {
        const auto alive = weak.lock();
        if (!alive) return;
        TreeView& view = **alive;
        if (view.wnd_ == nullptr || view.generation_ != generation) return;
        work(view, changed);
    };
}

void TreeView::open_in_explorer(std::uint32_t node) noexcept {
    try {
        std::wstring path;
        tree_.build_path(node, path);
        actions::open_in_explorer(std::move(path), tree_.node(node).has(model::node_container));
    } catch (...) {
    }
}

void TreeView::copy_path(std::uint32_t node) noexcept {
    try {
        tree_.build_path(node, path_);
        if (!actions::copy_text(wnd_, path_)) MessageBeep(MB_ICONWARNING);
    } catch (...) {
    }
}

void TreeView::delete_node(std::uint32_t node, bool permanent) noexcept {
    const model::Node& n = tree_.node(node);
    if (n.has(model::node_root) || n.parent == model::no_node) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    try {
        // Afterwards select what Explorer would: the next sibling, else the previous one, else
        // the parent folder.
        const std::uint32_t parent = n.parent;
        const model::Node& p = tree_.node(parent);
        std::wstring next;
        if (node + 1 < p.first_child + p.child_count) {
            next.assign(tree_.node(node + 1).name_view());
        } else if (node > p.first_child) {
            next.assign(tree_.node(node - 1).name_view());
        }
        std::wstring path;
        tree_.build_path(node, path);
        std::wstring name(n.name_view());
        actions::delete_path(
            std::move(path), permanent, wnd_,
            guard([parent, next = std::move(next), name = std::move(name)](TreeView& view,
                                                                         bool changed) {
                // If the delete was cancelled the item is still there: keep it selected.
                if (changed) view.reload_and_select(parent, name, next);
            }));
    } catch (...) {
    }
}

void TreeView::refresh_node(std::uint32_t node) noexcept {
    try {
        const model::Node& n = tree_.node(node);
        if (n.has(model::node_container)) {
            // Keep a selected direct child selected; otherwise the folder.
            std::wstring keep;
            if (selected_row_ >= 0) {
                const auto selected = tree_.node_at_row(static_cast<std::size_t>(selected_row_));
                if (tree_.node(selected).parent == node) keep.assign(tree_.node(selected).name_view());
            }
            reload_and_select(node, std::move(keep), {});
        } else if (n.parent != model::no_node) {
            reload_and_select(n.parent, std::wstring(n.name_view()), {});
        }
    } catch (...) {
    }
}

void TreeView::reload_and_select(std::uint32_t folder, std::wstring name,
                                 std::wstring fallback) noexcept {
    if (folder >= tree_.node_count()) return;
    const model::Tree::ReloadResult result = tree_.reload(folder);
    // Listings for the forgotten children will be ignored; stop them early.
    std::erase_if(pending_, [this](const PendingListing& p) {
        if (tree_.node(p.node).has(model::node_loading)) return false;
        p.ticket.cancel();
        return true;
    });
    apply_splice(result.splice);
    pending_select_ = {folder, std::move(name), std::move(fallback)};
    if (result.needs_load) {
        try {
            request_listing(folder);
        } catch (...) {
            tree_.fail_load(folder);
        }
    }
    if (const auto row = tree_.row_of(folder)) invalidate_row(*row);
    // Not expanded: nothing to wait for. Still loading: on_listing finishes the job.
    if (!tree_.node(folder).has(model::node_loading)) apply_pending_select(folder);
}

void TreeView::apply_pending_select(std::uint32_t folder) noexcept {
    if (pending_select_.folder != folder) return;
    PendingSelect select = std::move(pending_select_);
    pending_select_ = {};
    std::uint32_t target = model::no_node;
    if (!select.name.empty()) target = tree_.find_child(folder, select.name);
    if (target == model::no_node && !select.fallback.empty()) {
        target = tree_.find_child(folder, select.fallback);
    }
    if (target == model::no_node) target = folder;
    if (const auto row = tree_.row_of(target)) select_row(*row);
}

} // namespace filetree::view
