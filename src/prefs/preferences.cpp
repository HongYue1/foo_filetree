// The Preferences page: Tools > Folder Tree. Tabs are child dialogs (see
// foobar2000-component-dev/references/preferences-pages.md). Everything here is global; Apply
// saves through settings::apply(), which tells every live panel what changed.

#include <helpers/foobar2000+atl.h>

#include <helpers/DarkMode.h>
#include <helpers/atl-misc.h>

#include <commctrl.h>

#include <array>
#include <string>

#include "../../resource.h"
#include "../actions/action_settings.h"
#include "../actions/presets.h"
#include "../guids.h"
#include "../settings/settings_store.h"
#include "../version.h"
#include "list_editors.h"
#include "prefs_util.h"

#pragma comment(lib, "comctl32.lib")

namespace filetree::prefs {
namespace {

constexpr int tab_count = 7;
constexpr const wchar_t* tab_names[tab_count] = {L"General", L"Roots",         L"Files", L"Folders",
                                                 L"Look",    L"Mouse && keys", L"Menu"};
constexpr int tab_dialogs[tab_count] = {IDD_TAB_GENERAL, IDD_TAB_ROOTS,   IDD_TAB_FILES,
                                        IDD_TAB_FOLDERS, IDD_TAB_LOOK,    IDD_TAB_ACTIONS,
                                        IDD_TAB_MENU};
constexpr int roots_tab = 1; //!< holds the drive check boxes

//! Everything the page edits, so "changed?" is one comparison.
struct PageState {
    settings::Settings settings;
    std::wstring temp_playlist;
    bool recursive{true};
    actions::Bindings bindings{actions::Bindings::defaults()};

    friend bool operator==(const PageState&, const PageState&) = default;
};

PageState stored_state() {
    PageState state;
    state.settings = settings::stored();
    state.temp_playlist =
        pfc::stringcvt::string_wide_from_utf8(actions::temp_playlist_name().c_str()).get_ptr();
    state.recursive = actions::recursive_by_default();
    state.bindings = actions::bindings();
    return state;
}

PageState default_state() {
    PageState state;
    state.temp_playlist = L"Folder Tree";
    return state;
}

const wchar_t* drive_type_name(UINT type) noexcept {
    switch (type) {
    case DRIVE_REMOVABLE: return L"removable";
    case DRIVE_FIXED: return L"local";
    case DRIVE_REMOTE: return L"network";
    case DRIVE_CDROM: return L"optical";
    case DRIVE_RAMDISK: return L"RAM disk";
    default: return L"";
    }
}

class PreferencesPage : public CDialogImpl<PreferencesPage>,
                        public preferences_page_instance,
                        private settings::Listener {
public:
    explicit PreferencesPage(preferences_page_callback::ptr callback) : callback_(callback) {}

    enum { IDD = IDD_PREFERENCES };

    t_uint32 get_state() override {
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (initialised_ && !(from_controls() == stored_state())) state |= preferences_state::changed;
        return state;
    }

    void apply() override {
        KillTimer(preview_timer);
        const PageState state = from_controls();
        actions::set_temp_playlist_name(
            pfc::stringcvt::string_utf8_from_wide(state.temp_playlist.c_str()).get_ptr());
        actions::set_recursive_by_default(state.recursive);
        actions::set_bindings(state.bindings);
        settings::apply(state.settings);
        to_controls(stored_state()); // show the clamped values
        callback_->on_state_changed();
    }

    void reset() override {
        to_controls(default_state());
        callback_->on_state_changed();
        SetTimer(preview_timer, preview_delay_ms);
    }

    BEGIN_MSG_MAP_EX(PreferencesPage)
        MSG_WM_INITDIALOG(on_init_dialog)
        MSG_WM_DRAWITEM(on_draw_item)
        MSG_WM_TIMER(on_timer)
        MSG_WM_DESTROY(on_destroy)
        MESSAGE_HANDLER_EX(WM_NOTIFY, on_notify)
        COMMAND_HANDLER_EX(IDC_LINE_SWATCH, BN_CLICKED, on_swatch)
        COMMAND_HANDLER_EX(IDC_STARTUP_BROWSE, BN_CLICKED, on_startup_browse)
        COMMAND_HANDLER_EX(IDC_MENU_LIST, LBN_SELCHANGE, on_menu_select)
        COMMAND_HANDLER_EX(IDC_MENU_UP, BN_CLICKED, on_menu_move)
        COMMAND_HANDLER_EX(IDC_MENU_DOWN, BN_CLICKED, on_menu_move)
        COMMAND_HANDLER_EX(IDC_MENU_SHOW, BN_CLICKED, on_menu_show)
        COMMAND_HANDLER_EX(IDC_FAV_LIST, LBN_SELCHANGE, on_fav_select)
        COMMAND_HANDLER_EX(IDC_FAV_ADD, BN_CLICKED, on_fav_add)
        COMMAND_HANDLER_EX(IDC_FAV_REMOVE, BN_CLICKED, on_fav_remove)
        COMMAND_HANDLER_EX(IDC_FAV_UP, BN_CLICKED, on_fav_move)
        COMMAND_HANDLER_EX(IDC_FAV_DOWN, BN_CLICKED, on_fav_move)
        COMMAND_HANDLER_EX(IDC_FAV_QUICK, BN_CLICKED, on_fav_quick)
        COMMAND_HANDLER_EX(IDC_HIDDEN_LIST, LBN_SELCHANGE, on_hidden_select)
        COMMAND_HANDLER_EX(IDC_HIDDEN_ADD, BN_CLICKED, on_hidden_add)
        COMMAND_HANDLER_EX(IDC_HIDDEN_REMOVE, BN_CLICKED, on_hidden_remove)
        COMMAND_HANDLER_EX(IDC_HIDDEN_DEFAULTS, BN_CLICKED, on_hidden_defaults)
        COMMAND_CODE_HANDLER_EX(EN_CHANGE, on_changed)
        COMMAND_CODE_HANDLER_EX(BN_CLICKED, on_changed)
        COMMAND_CODE_HANDLER_EX(CBN_SELCHANGE, on_changed)
    END_MSG_MAP()

private:
    BOOL on_init_dialog(CWindow, LPARAM) {
        dark_.AddDialogWithControls(*this);
        create_tabs();
        const HWND page = m_hWnd;
        fill_combo(page, IDC_FILTER_BOX,
                   {L"In the address bar", L"Floating over the tree", L"Off"});
        fill_combo(page, IDC_STARTUP,
                   {L"Restore the last state", L"All folders closed", L"Open this folder:",
                    L"Show the last played track"});
        fill_combo(page, IDC_FAV_PLACE, {L"Before the drives", L"After the drives"});
        fill_combo(page, IDC_LINES, {L"None", L"Connector lines", L"Indentation guides"});
        fill_combo(page, IDC_EXTENSIONS,
                   {L"Always show", L"Never show", L"Hide for playable files"});
        fill_combo(page, IDC_SORT_FIELD,
                   {L"Name (natural)", L"Name", L"Date modified", L"Size", L"Type"});
        fill_combo(page, IDC_TOOLTIPS,
                   {L"Off", L"Full name when it is cut off", L"Full path"});
        fill_combo(page, IDC_FILES,
                   {L"All files", L"Playable files only", L"No files (folders only)"});
        for (int id = IDC_BIND_FIRST; id < IDC_BIND_FIRST + 2 * int(actions::gesture_count); ++id) {
            const HWND combo = find_control(page, id);
            if (combo == nullptr) continue;
            const bool folder = (id - IDC_BIND_FIRST) % 2 == 0;
            for (const actions::Preset& preset : actions::presets(folder)) {
                ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preset.label));
            }
        }
        to_controls(stored_state());
        known_ = settings::stored();
        settings::subscribe(this);
        initialised_ = true;
        return FALSE;
    }

    void create_tabs() {
        const HWND strip = GetDlgItem(IDC_TABS);
        for (int i = 0; i < tab_count; ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(tab_names[i]);
            ::SendMessageW(strip, TCM_INSERTITEMW, static_cast<WPARAM>(i),
                           reinterpret_cast<LPARAM>(&item));
        }
        // Only the strip: otherwise the control paints an empty page frame under it.
        RECT item{};
        RECT client{};
        ::SendMessageW(strip, TCM_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&item));
        ::GetClientRect(strip, &client);
        ::SetWindowPos(strip, nullptr, 0, 0, client.right, item.bottom + 2,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        RECT host{};
        ::GetWindowRect(GetDlgItem(IDC_PAGE_HOST), &host);
        ::MapWindowPoints(nullptr, m_hWnd, reinterpret_cast<POINT*>(&host), 2);
        for (int i = 0; i < tab_count; ++i) {
            const HWND tab = ::CreateDialogParamW(core_api::get_my_instance(),
                                                  MAKEINTRESOURCEW(tab_dialogs[i]), m_hWnd,
                                                  &PreferencesPage::tab_proc, 0);
            tabs_[static_cast<std::size_t>(i)] = tab;
            if (tab == nullptr) continue;
            if (i == roots_tab) create_drive_checks(tab); // before the dark hooks, so they theme them
            ::SetWindowPos(tab, GetDlgItem(IDC_PAGE_HOST), host.left, host.top,
                           host.right - host.left, host.bottom - host.top, SWP_NOACTIVATE);
            dark_.AddDialogWithControls(tab);
            pad_edits(tab);
        }
        show_tab(0);
    }

    //! One check box per present drive under the "Drives" label: four per row with the drive
    //! type, seven per row with just the letter when there are more than twelve drives (so all
    //! 26 fit). GetDriveTypeW reads no media, so this is safe here.
    void create_drive_checks(HWND tab) {
        RECT label{};
        ::GetWindowRect(::GetDlgItem(tab, IDC_DRIVES_LABEL), &label);
        ::MapWindowPoints(nullptr, tab, reinterpret_cast<POINT*>(&label), 2);
        const DWORD drives = GetLogicalDrives();
        int count = 0;
        for (int letter = 0; letter < 26; ++letter) count += (drives >> letter) & 1;
        const bool compact = count > 12;
        const int columns = compact ? 7 : 4;
        RECT unit{0, 0, compact ? 40 : 70, compact ? 12 : 14}; // column width, row pitch (DU)
        ::MapDialogRect(tab, &unit);
        RECT box{0, 0, compact ? 36 : 66, 10};
        ::MapDialogRect(tab, &box);
        const auto font = static_cast<WPARAM>(::SendMessageW(tab, WM_GETFONT, 0, 0));
        int slot = 0;
        for (int letter = 0; letter < 26; ++letter) {
            if ((drives & (1u << letter)) == 0) continue;
            const wchar_t root[] = {static_cast<wchar_t>(L'A' + letter), L':', L'\\', L'\0'};
            std::wstring text{root[0], L':'};
            if (const wchar_t* type = drive_type_name(GetDriveTypeW(root));
                !compact && *type != L'\0') {
                text += L" (";
                text += type;
                text += L")";
            }
            const int x = label.left + (slot % columns) * unit.right;
            const int y = label.bottom + MulDiv(unit.bottom, 1, 2) + (slot / columns) * unit.bottom;
            const HWND check = ::CreateWindowExW(
                0, L"BUTTON", text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                x, y, box.right, box.bottom, tab,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DRIVE_FIRST + letter)),
                core_api::get_my_instance(), nullptr);
            ::SendMessageW(check, WM_SETFONT, font, FALSE);
            ++slot;
        }
    }

    //! Tabs keep nothing: commands, owner-draw and notifications go to the page.
    static INT_PTR CALLBACK tab_proc(HWND tab, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_INITDIALOG: return FALSE;
        case WM_COMMAND:
        case WM_DRAWITEM: ::SendMessageW(::GetParent(tab), msg, wp, lp); return TRUE;
        default: return FALSE;
        }
    }

    //! Raw WM_NOTIFY with a pointer guard: pages have been sent WM_NOTIFY with lParam 0 / 0x4E.
    LRESULT on_notify(UINT, WPARAM, LPARAM lp) {
        SetMsgHandled(FALSE);
        if (lp < 0x10000) return 0;
        const auto* header = reinterpret_cast<const NMHDR*>(lp);
        if (header->idFrom == IDC_TABS && header->code == TCN_SELCHANGE) {
            show_tab(static_cast<int>(::SendMessageW(header->hwndFrom, TCM_GETCURSEL, 0, 0)));
            SetMsgHandled(TRUE);
        }
        return 0;
    }

    void show_tab(int index) {
        for (int i = 0; i < tab_count; ++i) {
            if (const HWND tab = tabs_[static_cast<std::size_t>(i)]) {
                ::ShowWindow(tab, i == index ? SW_SHOWNA : SW_HIDE);
            }
        }
    }

    void on_changed(UINT, int id, CWindow) {
        if (!initialised_ || updating_) return;
        if (id == IDC_LINE_HEX) ::InvalidateRect(find_control(m_hWnd, IDC_LINE_SWATCH), nullptr, FALSE);
        update_enabled();
        callback_->on_state_changed();
        // Live preview, debounced so typing a pattern does not relist on every key.
        SetTimer(preview_timer, preview_delay_ms);
    }

    void on_timer(UINT_PTR id) {
        if (id != preview_timer) {
            SetMsgHandled(FALSE);
            return;
        }
        KillTimer(preview_timer);
        try {
            settings::preview(from_controls().settings);
        } catch (...) {
        }
    }

    //! Leaving without Apply (Cancel, another page) drops the preview.
    //! A panel changed the favourites or hidden folders (context menu) while the page is open:
    //! show it, unless that list has edits of its own here.
    void on_settings_changed(std::uint32_t) noexcept override {
        if (!initialised_) return;
        try {
            const settings::Settings& now = settings::stored();
            const auto follow = [](PathListEditor& editor, const std::vector<std::wstring>& before,
                                   const std::vector<std::wstring>& after) {
                if (after == before || editor.paths != before) return;
                editor.paths = after;
                editor.show(std::max(editor.selection(), 0));
            };
            follow(favourites_, known_.favourites, now.favourites);
            if (now.favourite_files != known_.favourite_files &&
                favourites_.files == known_.favourite_files) {
                favourites_.files = now.favourite_files;
                favourites_.show(std::max(favourites_.selection(), 0));
            }
            follow(hidden_, known_.hidden_folders, now.hidden_folders);
            if (now.quick_favourites != known_.quick_favourites && quick_ == known_.quick_favourites) {
                quick_ = now.quick_favourites;
                show_quick();
            }
            known_ = now;
            callback_->on_state_changed();
        } catch (...) {
        }
    }

    void on_destroy() {
        settings::unsubscribe(this);
        KillTimer(preview_timer);
        settings::end_preview();
        SetMsgHandled(FALSE);
    }

    void on_swatch(UINT, int, CWindow) {
        COLORREF colour = parse_hex(get_text(m_hWnd, IDC_LINE_HEX), settings::stored().line_colour);
        if (!pick_colour(m_hWnd, colour)) return;
        set_check(m_hWnd, IDC_LINE_CUSTOM, true);
        set_check(m_hWnd, IDC_LINE_FOLLOW, false);
        set_text(m_hWnd, IDC_LINE_HEX, format_hex(colour)); // EN_CHANGE does the rest
        update_enabled();
    }

    void on_draw_item(UINT, LPDRAWITEMSTRUCT item) {
        if (item->CtlID != IDC_LINE_SWATCH) {
            SetMsgHandled(FALSE);
            return;
        }
        draw_swatch(*item, parse_hex(get_text(m_hWnd, IDC_LINE_HEX), settings::stored().line_colour));
    }

    // List editors (list_editors.cpp); each marks the page changed when it changed something.

    void on_menu_select(UINT, int, CWindow) { menu_.on_select(); }
    void on_menu_move(UINT, int id, CWindow) {
        if (menu_.move(id == IDC_MENU_UP)) on_changed(0, id, nullptr);
    }
    void on_menu_show(UINT, int id, CWindow) {
        if (!updating_ && menu_.toggle_shown()) on_changed(0, id, nullptr);
    }
    void on_fav_select(UINT, int, CWindow) {
        favourites_.on_select();
        show_quick();
    }
    void on_fav_add(UINT, int id, CWindow) {
        if (favourites_.add()) on_changed(0, id, nullptr);
        show_quick();
    }
    void on_fav_remove(UINT, int id, CWindow) {
        if (favourites_.remove()) on_changed(0, id, nullptr);
        show_quick();
    }
    void on_fav_move(UINT, int id, CWindow) {
        if (favourites_.move(id == IDC_FAV_UP)) on_changed(0, id, nullptr);
    }
    //! Index of the selected favourite in quick_, or -1.
    [[nodiscard]] int quick_index() const {
        const std::wstring* path = favourites_.selected_path();
        if (path == nullptr) return -1;
        for (std::size_t i = 0; i < quick_.size(); ++i) {
            if (settings::same_path(quick_[i], *path)) return static_cast<int>(i);
        }
        return -1;
    }
    //! The drop-down check box follows the selected favourite.
    void show_quick() noexcept {
        try {
            enable(m_hWnd, IDC_FAV_QUICK, favourites_.selected_path() != nullptr);
            set_check(m_hWnd, IDC_FAV_QUICK, quick_index() >= 0);
        } catch (...) {
        }
    }
    void on_fav_quick(UINT, int id, CWindow) {
        const std::wstring* path = favourites_.selected_path();
        if (updating_ || path == nullptr) return;
        const int index = quick_index();
        if (get_check(m_hWnd, IDC_FAV_QUICK) == (index >= 0)) return;
        if (index >= 0) {
            quick_.erase(quick_.begin() + index);
        } else {
            quick_.push_back(*path);
        }
        on_changed(0, id, nullptr);
    }

    void on_hidden_select(UINT, int, CWindow) { hidden_.on_select(); }
    void on_hidden_add(UINT, int id, CWindow) {
        if (hidden_.add()) on_changed(0, id, nullptr);
    }
    void on_hidden_remove(UINT, int id, CWindow) {
        if (hidden_.remove()) on_changed(0, id, nullptr);
    }
    void on_hidden_defaults(UINT, int id, CWindow) {
        if (hidden_.replace(settings::default_hidden_folders())) on_changed(0, id, nullptr);
    }

    void update_enabled() {
        const HWND page = m_hWnd;
        const bool lines = get_combo(page, IDC_LINES, 0) != 0;
        const bool custom = get_check(page, IDC_LINE_CUSTOM);
        enable(page, IDC_LINE_THICKNESS, lines);
        enable(page, IDC_LINE_FOLLOW, lines);
        enable(page, IDC_LINE_CUSTOM, lines);
        enable(page, IDC_LINE_OPACITY, lines && !custom);
        enable(page, IDC_LINE_SWATCH, lines && custom);
        enable(page, IDC_LINE_HEX, lines && custom);
        const bool folder = get_combo(page, IDC_STARTUP, 0) ==
                            static_cast<int>(settings::Startup::folder);
        enable(page, IDC_STARTUP_FOLDER, folder);
        enable(page, IDC_STARTUP_BROWSE, folder);
        const bool drives = get_check(page, IDC_DRIVES_LABEL);
        for (int letter = 0; letter < 26; ++letter) enable(page, IDC_DRIVE_FIRST + letter, drives);
    }

    void on_startup_browse(UINT, int, CWindow) {
        std::wstring path = get_text(m_hWnd, IDC_STARTUP_FOLDER);
        if (pick_folder(m_hWnd, path)) set_text(m_hWnd, IDC_STARTUP_FOLDER, path); // EN_CHANGE
    }

    [[nodiscard]] PageState from_controls() const {
        const HWND page = m_hWnd;
        PageState state = stored_state();
        settings::Settings& s = state.settings;
        s.show_drives = get_check(page, IDC_DRIVES_LABEL);
        for (int letter = 0; letter < 26; ++letter) {
            if (find_control(page, IDC_DRIVE_FIRST + letter) == nullptr) continue;
            const std::uint32_t bit = 1u << letter;
            s.hidden_drives = get_check(page, IDC_DRIVE_FIRST + letter) ? s.hidden_drives & ~bit
                                                                        : s.hidden_drives | bit;
        }
        state.temp_playlist = get_text(page, IDC_TEMP_PLAYLIST);
        if (state.temp_playlist.find_first_not_of(L" \t") == std::wstring::npos) {
            state.temp_playlist = L"Folder Tree";
        }
        state.recursive = get_check(page, IDC_RECURSIVE);
        s.show_address_bar = get_check(page, IDC_SHOW_ADDRESS_BAR);
        s.filter_box = static_cast<settings::FilterBox>(get_combo(page, IDC_FILTER_BOX, 0));
        s.startup = static_cast<settings::Startup>(get_combo(page, IDC_STARTUP, 0));
        s.startup_folder = get_text(page, IDC_STARTUP_FOLDER);
        s.watch_changes = get_check(page, IDC_WATCH_CHANGES);

        s.lines = static_cast<settings::TreeLines>(get_combo(page, IDC_LINES, 0));
        s.line_thickness = get_int(page, IDC_LINE_THICKNESS, 1, 4, s.line_thickness);
        s.line_custom_colour = get_check(page, IDC_LINE_CUSTOM);
        s.line_opacity = get_int(page, IDC_LINE_OPACITY, 10, 100, s.line_opacity);
        s.line_colour = parse_hex(get_text(page, IDC_LINE_HEX), s.line_colour);
        s.row_padding = get_int(page, IDC_ROW_PADDING, 0, 12, s.row_padding);
        s.icon_size = get_int(page, IDC_ICON_SIZE, 12, 24, s.icon_size);
        s.mark_size = get_int(page, IDC_MARK_SIZE, 8, 16, s.mark_size);
        s.show_icons = get_check(page, IDC_SHOW_ICONS);
        s.mark_favourites = get_check(page, IDC_MARK_FAVOURITES);
        s.mark_playing = get_check(page, IDC_MARK_PLAYING);
        s.follow_playing = get_check(page, IDC_FOLLOW_PLAYING);
        s.read_only = get_check(page, IDC_READ_ONLY);
        s.extensions = static_cast<settings::Extensions>(get_combo(page, IDC_EXTENSIONS, 0));
        s.sort.field = static_cast<model::SortField>(get_combo(page, IDC_SORT_FIELD, 0));
        s.sort.folders_first = get_check(page, IDC_FOLDERS_FIRST);
        s.sort.reverse = get_check(page, IDC_SORT_REVERSE);
        s.tooltips = static_cast<settings::Tooltips>(get_combo(page, IDC_TOOLTIPS, 1));
        s.hover_highlight = get_check(page, IDC_HOVER_HIGHLIGHT);
        s.zebra = get_check(page, IDC_ZEBRA);
        s.disk_search = get_check(page, IDC_DISK_SEARCH);
        s.thin_scrollbar = get_check(page, IDC_THIN_SCROLLBAR);
        s.show_status_bar = get_check(page, IDC_SHOW_STATUS_BAR);
        s.status_counters = get_check(page, IDC_STATUS_COUNTERS);
        s.transparent = get_check(page, IDC_TRANSPARENT);

        s.files = static_cast<fs::FileMode>(get_combo(page, IDC_FILES, 1));
        s.show_hidden = get_check(page, IDC_SHOW_HIDDEN);
        s.show_system = get_check(page, IDC_SHOW_SYSTEM);
        s.always_show = get_text(page, IDC_ALWAYS_SHOW);
        s.never_show = get_text(page, IDC_NEVER_SHOW);
        s.hide_patterns = get_text(page, IDC_HIDE_PATTERNS);
        s.menu = menu_.layout;
        s.favourites = favourites_.paths;
        s.favourite_files = favourites_.files;
        s.quick_favourites = quick_; // sanitize drops the ones no longer favourites
        s.library_roots = get_check(page, IDC_LIBRARY_ROOTS);
        s.hidden_folders = hidden_.paths;
        s.hide_empty = get_check(page, IDC_HIDE_EMPTY);
        s.favourites_place =
            static_cast<settings::FavouritesPlace>(get_combo(page, IDC_FAV_PLACE, 0));
        s.separate_favourites = get_check(page, IDC_FAV_SEPARATE);
        s.favourites_gap = get_int(page, IDC_FAV_GAP, 0, 24, s.favourites_gap);
        s.sanitize();

        for (std::size_t g = 0; g < actions::gesture_count; ++g) {
            actions::Binding& binding = state.bindings.gestures[g];
            const int id = IDC_BIND_FIRST + static_cast<int>(g) * 2;
            binding.folder = actions::from_preset(
                static_cast<std::size_t>(get_combo(page, id, 0)), true, binding.folder);
            binding.file = actions::from_preset(
                static_cast<std::size_t>(get_combo(page, id + 1, 0)), false, binding.file);
        }
        return state;
    }

    void to_controls(const PageState& state) {
        updating_ = true;
        const HWND page = m_hWnd;
        const settings::Settings& s = state.settings;
        set_check(page, IDC_DRIVES_LABEL, s.show_drives);
        for (int letter = 0; letter < 26; ++letter) {
            set_check(page, IDC_DRIVE_FIRST + letter, (s.hidden_drives & (1u << letter)) == 0);
        }
        set_text(page, IDC_TEMP_PLAYLIST, state.temp_playlist);
        set_check(page, IDC_RECURSIVE, state.recursive);
        set_check(page, IDC_SHOW_ADDRESS_BAR, s.show_address_bar);
        set_combo(page, IDC_FILTER_BOX, static_cast<int>(s.filter_box));
        set_combo(page, IDC_STARTUP, static_cast<int>(s.startup));
        set_text(page, IDC_STARTUP_FOLDER, s.startup_folder);
        set_check(page, IDC_WATCH_CHANGES, s.watch_changes);

        set_combo(page, IDC_LINES, static_cast<int>(s.lines));
        set_int(page, IDC_LINE_THICKNESS, s.line_thickness);
        set_check(page, IDC_LINE_FOLLOW, !s.line_custom_colour);
        set_check(page, IDC_LINE_CUSTOM, s.line_custom_colour);
        set_int(page, IDC_LINE_OPACITY, s.line_opacity);
        set_text(page, IDC_LINE_HEX, format_hex(s.line_colour));
        set_int(page, IDC_ROW_PADDING, s.row_padding);
        set_int(page, IDC_ICON_SIZE, s.icon_size);
        set_int(page, IDC_MARK_SIZE, s.mark_size);
        set_check(page, IDC_SHOW_ICONS, s.show_icons);
        set_check(page, IDC_MARK_FAVOURITES, s.mark_favourites);
        set_check(page, IDC_MARK_PLAYING, s.mark_playing);
        set_check(page, IDC_FOLLOW_PLAYING, s.follow_playing);
        set_check(page, IDC_READ_ONLY, s.read_only);
        set_combo(page, IDC_EXTENSIONS, static_cast<int>(s.extensions));
        set_combo(page, IDC_SORT_FIELD, static_cast<int>(s.sort.field));
        set_check(page, IDC_FOLDERS_FIRST, s.sort.folders_first);
        set_check(page, IDC_SORT_REVERSE, s.sort.reverse);
        set_combo(page, IDC_TOOLTIPS, static_cast<int>(s.tooltips));
        set_check(page, IDC_HOVER_HIGHLIGHT, s.hover_highlight);
        set_check(page, IDC_ZEBRA, s.zebra);
        set_check(page, IDC_DISK_SEARCH, s.disk_search);
        set_check(page, IDC_THIN_SCROLLBAR, s.thin_scrollbar);
        set_check(page, IDC_SHOW_STATUS_BAR, s.show_status_bar);
        set_check(page, IDC_STATUS_COUNTERS, s.status_counters);
        set_check(page, IDC_TRANSPARENT, s.transparent);

        set_combo(page, IDC_FILES, static_cast<int>(s.files));
        set_check(page, IDC_SHOW_HIDDEN, s.show_hidden);
        set_check(page, IDC_SHOW_SYSTEM, s.show_system);
        set_text(page, IDC_ALWAYS_SHOW, s.always_show);
        set_text(page, IDC_NEVER_SHOW, s.never_show);
        set_text(page, IDC_HIDE_PATTERNS, s.hide_patterns);
        for (std::size_t g = 0; g < actions::gesture_count; ++g) {
            const actions::Binding& binding = state.bindings.gestures[g];
            const int id = IDC_BIND_FIRST + static_cast<int>(g) * 2;
            set_combo(page, id, static_cast<int>(actions::preset_index(binding.folder, true)));
            set_combo(page, id + 1, static_cast<int>(actions::preset_index(binding.file, false)));
        }
        menu_.layout = s.menu;
        favourites_.paths = s.favourites;
        favourites_.files = s.favourite_files;
        set_check(page, IDC_LIBRARY_ROOTS, s.library_roots);
        hidden_.paths = s.hidden_folders;
        set_check(page, IDC_HIDE_EMPTY, s.hide_empty);
        set_combo(page, IDC_FAV_PLACE, static_cast<int>(s.favourites_place));
        set_check(page, IDC_FAV_SEPARATE, s.separate_favourites);
        set_int(page, IDC_FAV_GAP, s.favourites_gap);
        menu_.show(std::max(menu_.selection(), 0));
        favourites_.show(std::max(favourites_.selection(), 0));
        quick_ = s.quick_favourites;
        show_quick();
        hidden_.show(std::max(hidden_.selection(), 0));
        ::InvalidateRect(find_control(page, IDC_LINE_SWATCH), nullptr, FALSE);
        updating_ = false;
        update_enabled();
    }

    static constexpr UINT_PTR preview_timer = 1;
    static constexpr UINT preview_delay_ms = 250;

    const preferences_page_callback::ptr callback_;
    std::array<HWND, tab_count> tabs_{};
    MenuEditor menu_{m_hWnd};
    PathListEditor favourites_{m_hWnd, {IDC_FAV_LIST, IDC_FAV_REMOVE, IDC_FAV_UP, IDC_FAV_DOWN},
                               L"\x2014 Favourite files \x2014"};
    PathListEditor hidden_{m_hWnd, {IDC_HIDDEN_LIST, IDC_HIDDEN_REMOVE}};
    bool initialised_{false};
    bool updating_{false};
    std::vector<std::wstring> quick_; //!< favourites drop-down (folders and files)
    settings::Settings known_; //!< stored settings as last seen, to follow outside changes
    // A member: it hooks this dialog and its controls for the lifetime of both.
    fb2k::CDarkModeHooks dark_;
};

class PreferencesPageFactory : public preferences_page_impl<PreferencesPage> {
public:
    const char* get_name() override { return FILETREE_NAME; }
    GUID get_guid() override { return guids::preferences_page; }
    GUID get_parent_guid() override { return preferences_page::guid_tools; }
};

preferences_page_factory_t<PreferencesPageFactory> g_preferences_page_factory;

} // namespace
} // namespace filetree::prefs

