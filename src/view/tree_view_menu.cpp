// TreeView context menu and the Explorer-style commands behind it (open, copy path, delete,
// refresh). Rename's inline editor is in inline_edit.cpp.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <windowsx.h>

#include <algorithm>

#include "../actions/file_ops.h"
#include "../actions/fb2k_menu.h"
#include "../actions/shell_menu.h"
#include "../actions/shell_ops.h"
#include "../guids.h"

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
    id_undo,
    id_favourite,
    id_new_folder,
    id_paste,
    id_cut,
    id_copy,
    id_queue,
    id_save_playlist,
    id_open_with,
    id_properties,
    id_hide_folder,
    id_quick_favourite,
    // Empty-area menu.
    id_refresh_all,
    id_collapse_all,
    id_preferences,
};

//! Index of `path` in a path list (favourites, hidden folders) (case-insensitive, as NTFS), or -1.
int path_index(const std::vector<std::wstring>& list, std::wstring_view path) {
    const std::wstring clean = settings::clean_path(path);
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (CompareStringOrdinal(list[i].c_str(), static_cast<int>(list[i].size()), clean.c_str(),
                                 static_cast<int>(clean.size()), TRUE) == CSTR_EQUAL) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

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
        if (selected_row_ < 0) {
            POINT corner{metrics_.indent, metrics_.row_height};
            ClientToScreen(wnd_, &corner);
            show_background_menu(corner);
            return;
        }
        row = selected_row_;
        ensure_visible(static_cast<std::size_t>(row));
        const model::Node& n = tree_.node(tree_.node_at_row(static_cast<std::size_t>(row)));
        point = {text_left(n.depth),
                 row_top(static_cast<std::size_t>(row)) + metrics_.row_height};
        ClientToScreen(wnd_, &point);
    } else {
        POINT client = point;
        ScreenToClient(wnd_, &client);
        row = row_at(client.y);
        if (row < 0) {
            if (GetFocus() != wnd_) SetFocus(wnd_);
            show_background_menu(point);
            return;
        }
        if (GetFocus() != wnd_) SetFocus(wnd_);
        // As Explorer: a right-click inside the selection keeps it, elsewhere selects one row.
        if (tree_.is_selected(tree_.node_at_row(static_cast<std::size_t>(row)))) {
            focus_row(static_cast<std::size_t>(row));
        } else {
            select_row(static_cast<std::size_t>(row));
        }
    }

    const std::uint32_t node = tree_.node_at_row(static_cast<std::size_t>(row));
    const model::Node& n = tree_.node(node);
    const bool folder = n.has(model::node_container);
    const bool root = n.has(model::node_root);

    try {
        std::wstring path;
        tree_.build_path(node, path);
        // Most items act on the whole selection (actions_for); single-item ones are greyed.
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        const bool several = nodes.size() > 1;
        const UINT single_flags = several ? MF_GRAYED : 0u;
        bool any_file = false;
        bool any_folder = false;
        bool any_drive = false;
        bool any_root = false;
        bool any_virtual = false; // "Favourite files": playlist commands and Refresh only
        for (const std::uint32_t item : nodes) {
            const model::Node& i = tree_.node(item);
            any_virtual |= i.has(model::node_virtual);
            (i.has(model::node_container) ? any_folder : any_file) = true;
            any_root |= i.has(model::node_root);
            if (i.has(model::node_root)) any_drive |= !i.has(model::node_favourite);
        }
        const UINT drive_flags = any_drive ? MF_GRAYED : 0u;
        const int fav_state = favourite_state(nodes);
        const bool any_hideable = std::any_of(nodes.begin(), nodes.end(), [&](std::uint32_t n) {
            const model::Node& i = tree_.node(n);
            return i.has(model::node_container) && !i.has(model::node_root);
        });
        const UINT root_flags_all = any_root ? MF_GRAYED : 0u;

        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) return;
        MenuSession session;
        const UINT root_flags = root ? MF_GRAYED : 0u;
        // The user's order (Preferences > Menu); a separator wherever the group changes.
        const settings::MenuLayout& layout = settings::current().menu;
        int last_group = -1;
        for (const settings::MenuItem item : layout.order) {
            if (!layout.visible(item)) continue;
            using settings::MenuItem;
            if (item == MenuItem::undo && undo_.kind == UndoRecord::Kind::none) continue;
            if (item == MenuItem::fb2k_menu && !any_file) continue;
            if (item == MenuItem::favourite && fav_state < 0) continue;
            if (item == MenuItem::hide_folder && !any_hideable) continue;
            if (item == MenuItem::save_playlist && !any_folder && !several) continue;
            if (item == MenuItem::open_with && folder) continue;
            if (item == MenuItem::paste && !actions::clipboard_has_files()) continue;
            if (read_only_ && settings::changes_files(item)) continue;
            if (any_virtual && item != MenuItem::play && item != MenuItem::add_active &&
                item != MenuItem::new_playlist && item != MenuItem::queue &&
                item != MenuItem::refresh) {
                continue;
            }
            const int group = settings::menu_group(item);
            if (last_group >= 0 && group != last_group) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            last_group = group;
            switch (item) {
            case MenuItem::play: AppendMenuW(menu, MF_STRING, id_play, L"Play"); break;
            case MenuItem::add_active:
                AppendMenuW(menu, MF_STRING, id_add_active, L"Add to active playlist");
                break;
            case MenuItem::new_playlist:
                AppendMenuW(menu, MF_STRING, id_new_playlist, L"Send to new playlist");
                break;
            case MenuItem::queue:
                AppendMenuW(menu, MF_STRING, id_queue, L"Add to playback queue");
                break;
            case MenuItem::save_playlist:
                AppendMenuW(menu, MF_STRING, id_save_playlist, L"Save as playlist...");
                break;
            case MenuItem::open_with:
                AppendMenuW(menu, MF_STRING | single_flags, id_open_with, L"Open with...");
                break;
            case MenuItem::properties:
                AppendMenuW(menu, MF_STRING, id_properties, L"Properties\tAlt+Enter");
                break;
            case MenuItem::open_explorer:
                AppendMenuW(menu, MF_STRING, id_open_explorer,
                            folder ? L"Open in Explorer" : L"Show in folder");
                break;
            case MenuItem::copy_path:
                AppendMenuW(menu, MF_STRING, id_copy_path, L"Copy path\tCtrl+Shift+C");
                break;
            case MenuItem::new_folder:
                AppendMenuW(menu, MF_STRING | single_flags, id_new_folder,
                            folder ? L"New folder\tF7" : L"New folder here\tF7");
                break;
            case MenuItem::cut:
                AppendMenuW(menu, MF_STRING | drive_flags, id_cut, L"Cut\tCtrl+X");
                break;
            case MenuItem::copy:
                AppendMenuW(menu, MF_STRING | drive_flags, id_copy, L"Copy\tCtrl+C");
                break;
            case MenuItem::paste:
                AppendMenuW(menu, MF_STRING, id_paste, folder ? L"Paste\tCtrl+V" : L"Paste here\tCtrl+V");
                break;
            case MenuItem::rename:
                AppendMenuW(menu, MF_STRING | root_flags | single_flags, id_rename, L"Rename\tF2");
                break;
            case MenuItem::remove:
                AppendMenuW(menu, MF_STRING | root_flags_all, id_delete, L"Delete\tDel");
                break;
            case MenuItem::refresh: AppendMenuW(menu, MF_STRING, id_refresh, L"Refresh\tF5"); break;
            case MenuItem::favourite: {
                AppendMenuW(menu, MF_STRING, id_favourite,
                            fav_state == 1 ? L"Remove from favourites" : L"Add to favourites");
                if (fav_state == 1) {
                    AppendMenuW(menu, MF_STRING, id_quick_favourite,
                                quick_state(nodes) ? L"Remove from favourites drop-down"
                                                   : L"Add to favourites drop-down");
                }
                break;
            }
            case MenuItem::hide_folder:
                AppendMenuW(menu, MF_STRING, id_hide_folder,
                            several ? L"Hide these folders" : L"Hide this folder");
                break;
            case MenuItem::undo: {
                const bool rename = undo_.kind == UndoRecord::Kind::rename;
                std::wstring label =
                    rename                   ? L"Undo rename of \"" + undo_.old_name + L"\""
                    : undo_.paths.size() > 1 ? L"Undo delete of " + std::to_wstring(undo_.paths.size()) + L" items"
                                             : L"Undo delete of \"" + undo_.name + L"\"";
                label += L"\tCtrl+Z";
                AppendMenuW(menu, MF_STRING, id_undo, label.c_str());
                break;
            }
            case MenuItem::fb2k_menu: {
                HMENU fb2k = CreatePopupMenu();
                AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fb2k), L"foobar2000");
                std::vector<std::wstring> files; // the tracks among the items
                for (const std::uint32_t entry : nodes) {
                    if (tree_.node(entry).has(model::node_container)) continue;
                    tree_.build_path(entry, files.emplace_back());
                }
                session.fb2k.prepare(fb2k, std::move(files));
                break;
            }
            case MenuItem::explorer_menu: {
                HMENU shell = CreatePopupMenu();
                AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(shell), L"Explorer");
                // The shell's menu covers several items of one folder; otherwise the clicked one.
                std::vector<std::wstring> paths;
                if (several && same_parent(nodes)) {
                    paths_of(nodes, paths);
                } else {
                    paths.push_back(path);
                }
                session.shell.prepare(shell, std::move(paths), wnd_, GetKeyState(VK_SHIFT) < 0);
                break;
            }
            }
        }
        if (GetMenuItemCount(menu) <= 0) {
            DestroyMenu(menu);
            return;
        }

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

void TreeView::show_background_menu(POINT point) noexcept {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    AppendMenuW(menu, MF_STRING, id_refresh_all, L"Refresh all");
    AppendMenuW(menu, MF_STRING, id_collapse_all, L"Collapse all");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, id_preferences, L"Preferences...");
    const UINT id = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                                     point.x, point.y, 0, wnd_, nullptr));
    DestroyMenu(menu);
    switch (id) {
    case id_refresh_all: relist_all(); break;
    case id_collapse_all: collapse_all(); break;
    case id_preferences:
        try {
            ui_control::get()->show_preferences(guids::preferences_page);
        } catch (...) {
        }
        break;
    default: break;
    }
}

void TreeView::collapse_all() noexcept {
    try {
        std::uint32_t root = model::no_node;
        if (selected_row_ >= 0) {
            root = tree_.node_at_row(static_cast<std::size_t>(selected_row_));
            while (tree_.node(root).parent != model::no_node) root = tree_.node(root).parent;
        }
        apply_splice(tree_.collapse_all());
        if (root != model::no_node) select_node(root);
        scroll_to(0);
    } catch (...) {
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
    case id_queue:
        action.target = actions::Target::queue;
        action.mode = actions::Mode::add;
        send_node(action, node);
        break;
    case id_open_explorer: open_in_explorer(node); break;
    case id_save_playlist: save_as_playlist(node); break;
    case id_open_with:
        try {
            std::wstring path;
            tree_.build_path(node, path);
            actions::open_with(path, wnd_);
        } catch (...) {
        }
        break;
    case id_properties: show_properties(node); break;
    case id_copy_path: copy_path(node); break;
    case id_rename: begin_rename(node); break;
    case id_delete: delete_node(node, GetKeyState(VK_SHIFT) < 0); break;
    case id_refresh: refresh_open_folders(); break;
    case id_undo: undo(); break;
    case id_favourite: toggle_favourite(node); break;
    case id_hide_folder: hide_folder(node); break;
    case id_quick_favourite: toggle_quick_favourite(node); break;
    case id_new_folder: new_folder(node); break;
    case id_paste: paste_into(node); break;
    case id_cut: put_on_clipboard(node, true); break;
    case id_copy: put_on_clipboard(node, false); break;
    default: break;
    }
}

int TreeView::favourite_state(const std::vector<std::uint32_t>& nodes) const {
    const settings::Settings& s = settings::current();
    int state = -1;
    std::wstring path;
    for (const std::uint32_t n : nodes) {
        const model::Node& item = tree_.node(n);
        if (item.has(model::node_virtual)) continue;
        tree_.build_path(n, path);
        const bool listed =
            path_index(item.has(model::node_container) ? s.favourites : s.favourite_files,
                       path) >= 0;
        if (!listed) return 0;
        state = 1;
    }
    return state;
}

bool TreeView::quick_state(const std::vector<std::uint32_t>& nodes) const {
    const std::vector<std::wstring>& quick = settings::current().quick_favourites;
    std::wstring path;
    for (const std::uint32_t n : nodes) {
        if (tree_.node(n).has(model::node_virtual)) continue;
        tree_.build_path(n, path);
        if (path_index(quick, path) < 0) return false;
    }
    return true;
}

void TreeView::toggle_quick_favourite(std::uint32_t node) noexcept {
    try {
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        if (favourite_state(nodes) != 1) return;
        const bool remove = quick_state(nodes);
        settings::Settings next = settings::stored();
        std::wstring path;
        for (const std::uint32_t n : nodes) {
            if (tree_.node(n).has(model::node_virtual)) continue;
            tree_.build_path(n, path);
            const int index = path_index(next.quick_favourites, path);
            if (remove && index >= 0) {
                next.quick_favourites.erase(next.quick_favourites.begin() + index);
            } else if (!remove && index < 0) {
                next.quick_favourites.push_back(settings::clean_path(path));
            }
        }
        settings::apply(std::move(next));
    } catch (...) {
    }
}

void TreeView::toggle_favourite(std::uint32_t node) noexcept {
    try {
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        const int state = favourite_state(nodes);
        if (state < 0) return;
        settings::Settings next = settings::stored();
        std::wstring path;
        for (const std::uint32_t n : nodes) {
            const model::Node& item = tree_.node(n);
            if (item.has(model::node_virtual)) continue;
            tree_.build_path(n, path);
            std::vector<std::wstring>& list =
                item.has(model::node_container) ? next.favourites : next.favourite_files;
            const int index = path_index(list, path);
            if (state == 1 && index >= 0) {
                list.erase(list.begin() + index);
            } else if (state == 0 && index < 0) {
                list.push_back(settings::clean_path(path));
            }
        }
        // Every panel updates its roots (relist_all keeps what is open, selected and the
        // scroll position). `node` is not valid afterwards.
        settings::apply(std::move(next));
    } catch (...) {
    }
}

void TreeView::hide_folder(std::uint32_t node) noexcept {
    try {
        std::vector<std::uint32_t> nodes;
        actions_for(node, nodes);
        settings::Settings next = settings::stored();
        std::wstring path;
        bool added = false;
        for (const std::uint32_t n : nodes) {
            const model::Node& item = tree_.node(n);
            // Roots are never hidden; files have their own rules (Files tab).
            if (!item.has(model::node_container) || item.has(model::node_root)) continue;
            tree_.build_path(n, path);
            if (path_index(next.hidden_folders, path) < 0) {
                next.hidden_folders.push_back(settings::clean_path(path));
                added = true;
            }
        }
        if (added) settings::apply(std::move(next)); // every panel relists; `node` is gone
    } catch (...) {
    }
}

actions::ShellDone TreeView::guard(std::function<void(TreeView&, actions::ShellResult)> work) {
    std::weak_ptr<TreeView*> weak = alive_;
    return [weak, generation = generation_, work = std::move(work)](actions::ShellResult result) {
        const auto alive = weak.lock();
        if (!alive) return;
        TreeView& view = **alive;
        if (view.wnd_ == nullptr || view.generation_ != generation) return;
        work(view, result);
    };
}

void TreeView::undo() noexcept {
    if (refuse_change()) return;
    if (undo_.kind == UndoRecord::Kind::none) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    try {
        UndoRecord record = std::move(undo_);
        undo_ = {};
        std::wstring path = record.folder_path;
        if (!path.empty() && path.back() != L'\\') path.push_back(L'\\');
        path += record.name;
        const std::uint32_t folder = record.folder;
        if (record.kind == UndoRecord::Kind::rename) {
            actions::rename_path(
                std::move(path), record.old_name, wnd_,
                guard([folder, record](TreeView& view, actions::ShellResult result) {
                    if (result.ran) view.reload_and_select(folder, record.old_name, record.name);
                }));
        } else {
            std::vector<std::wstring> paths = record.paths;
            if (paths.empty()) paths.push_back(path);
            const std::size_t count = paths.size();
            actions::restore_recycled(
                std::move(paths),
                guard([folder, record, path, count](TreeView& view, actions::ShellResult result) {
                    if (result.ran) {
                        // Several items may come from other folders too: re-check every open one.
                        if (count > 1) view.refresh_open_folders();
                        view.reload_and_select(folder, record.name, {});
                    }
                    if (result.succeeded) return;
                    MessageBeep(MB_ICONWARNING);
                    if (count > 1) {
                        FB2K_console_formatter()
                            << "Folder Tree: some of the " << count
                            << " deleted items could not be restored from the Recycle Bin "
                               "(gone, or something exists there now).";
                    } else {
                        FB2K_console_formatter()
                            << "Folder Tree: could not restore "
                            << pfc::stringcvt::string_utf8_from_wide(path.c_str()).get_ptr()
                            << " from the Recycle Bin (gone, or something exists there now).";
                    }
                }));
        }
    } catch (...) {
    }
}

} // namespace filetree::view
