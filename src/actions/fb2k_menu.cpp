#include <helpers/foobar2000+atl.h>

#include "fb2k_menu.h"

#include "../version.h"

namespace filetree::actions {

struct Fb2kMenu::State {
    HMENU submenu{};
    std::vector<std::wstring> paths;
    bool populated{false};
    service_ptr_t<contextmenu_manager> manager;
};

Fb2kMenu::Fb2kMenu() : state_(std::make_unique<State>()) {}
Fb2kMenu::~Fb2kMenu() = default;

void Fb2kMenu::prepare(HMENU submenu, std::vector<std::wstring> paths) {
    state_->submenu = submenu;
    state_->paths = std::move(paths);
    state_->populated = false;
    state_->manager.release();
}

bool Fb2kMenu::handle_message(UINT msg, WPARAM wp, LPARAM, LRESULT& result) noexcept {
    State& s = *state_;
    if (msg != WM_INITMENUPOPUP || s.submenu == nullptr ||
        reinterpret_cast<HMENU>(wp) != s.submenu) {
        return false;
    }
    result = 0;
    if (s.populated) return true;
    s.populated = true;
    try {
        // A handle is just the location; creating it reads nothing from disk.
        metadb_handle_list handles;
        auto db = metadb::get();
        pfc::string8 canonical;
        for (const std::wstring& path : s.paths) {
            const pfc::stringcvt::string_utf8_from_wide utf8(path.c_str());
            filesystem::g_get_canonical_path(utf8, canonical);
            handles.add_item(db->handle_create(make_playable_location(canonical, 0)));
        }

        s.manager = contextmenu_manager::g_create();
        s.manager->init_context(handles, contextmenu_manager::flag_show_shortcuts);
        s.manager->win32_build_menu(s.submenu, static_cast<int>(first_id),
                                    static_cast<int>(id_count));
    } catch (const std::exception& error) {
        FB2K_console_formatter() << FILETREE_NAME << ": context menu failed: " << error.what();
    }
    if (GetMenuItemCount(s.submenu) <= 0) {
        AppendMenuW(s.submenu, MF_STRING | MF_GRAYED, 0, L"(unavailable)");
    }
    return true;
}

void Fb2kMenu::invoke(UINT id) noexcept {
    if (!owns(id) || !state_->manager.is_valid()) return;
    state_->manager->execute_by_id(id - first_id);
}

} // namespace filetree::actions
