// The Preferences page: Display > Folder Tree. Tabs are child dialogs (see
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
#include "../guids.h"
#include "../settings/settings_store.h"
#include "../version.h"
#include "prefs_util.h"

#pragma comment(lib, "comctl32.lib")

namespace filetree::prefs {
namespace {

constexpr int tab_count = 3;
constexpr const wchar_t* tab_names[tab_count] = {L"General", L"Display", L"Filter"};

//! Everything the page edits, so "changed?" is one comparison.
struct PageState {
    settings::Settings settings;
    std::wstring temp_playlist;
    bool recursive{true};

    friend bool operator==(const PageState&, const PageState&) = default;
};

PageState stored_state() {
    PageState state;
    state.settings = settings::current();
    state.temp_playlist =
        pfc::stringcvt::string_wide_from_utf8(actions::temp_playlist_name().c_str()).get_ptr();
    state.recursive = actions::recursive_by_default();
    return state;
}

PageState default_state() {
    PageState state;
    state.settings.menu = settings::current().menu; // edited on its own tab (M5c)
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

class PreferencesPage : public CDialogImpl<PreferencesPage>, public preferences_page_instance {
public:
    explicit PreferencesPage(preferences_page_callback::ptr callback) : callback_(callback) {}

    enum { IDD = IDD_PREFERENCES };

    t_uint32 get_state() override {
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (initialised_ && !(from_controls() == stored_state())) state |= preferences_state::changed;
        return state;
    }

    void apply() override {
        const PageState state = from_controls();
        actions::set_temp_playlist_name(
            pfc::stringcvt::string_utf8_from_wide(state.temp_playlist.c_str()).get_ptr());
        actions::set_recursive_by_default(state.recursive);
        settings::apply(state.settings);
        to_controls(stored_state()); // show the clamped values
        callback_->on_state_changed();
    }

    void reset() override {
        to_controls(default_state());
        callback_->on_state_changed();
    }

    BEGIN_MSG_MAP_EX(PreferencesPage)
        MSG_WM_INITDIALOG(on_init_dialog)
        MSG_WM_DRAWITEM(on_draw_item)
        MESSAGE_HANDLER_EX(WM_NOTIFY, on_notify)
        COMMAND_HANDLER_EX(IDC_LINE_SWATCH, BN_CLICKED, on_swatch)
        COMMAND_CODE_HANDLER_EX(EN_CHANGE, on_changed)
        COMMAND_CODE_HANDLER_EX(BN_CLICKED, on_changed)
        COMMAND_CODE_HANDLER_EX(CBN_SELCHANGE, on_changed)
    END_MSG_MAP()

private:
    BOOL on_init_dialog(CWindow, LPARAM) {
        dark_.AddDialogWithControls(*this);
        create_tabs();
        const HWND page = m_hWnd;
        fill_combo(page, IDC_LINES, {L"None", L"Connector lines", L"Indentation guides"});
        fill_combo(page, IDC_EXTENSIONS,
                   {L"Always show", L"Never show", L"Hide for playable files"});
        fill_combo(page, IDC_SORT_FIELD,
                   {L"Name (natural)", L"Name", L"Date modified", L"Size", L"Type"});
        fill_combo(page, IDC_FILES,
                   {L"All files", L"Playable files only", L"No files (folders only)"});
        to_controls(stored_state());
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
                                                  MAKEINTRESOURCEW(IDD_TAB_GENERAL + i), m_hWnd,
                                                  &PreferencesPage::tab_proc, 0);
            tabs_[static_cast<std::size_t>(i)] = tab;
            if (tab == nullptr) continue;
            if (i == 0) create_drive_checks(tab); // before the dark hooks, so they theme them
            ::SetWindowPos(tab, GetDlgItem(IDC_PAGE_HOST), host.left, host.top,
                           host.right - host.left, host.bottom - host.top, SWP_NOACTIVATE);
            dark_.AddDialogWithControls(tab);
            pad_edits(tab);
        }
        show_tab(0);
    }

    //! One check box per present drive, four per row under the "Drives" label. GetDriveTypeW
    //! reads no media, so this is safe here.
    void create_drive_checks(HWND tab) {
        RECT label{};
        ::GetWindowRect(::GetDlgItem(tab, IDC_DRIVES_LABEL), &label);
        ::MapWindowPoints(nullptr, tab, reinterpret_cast<POINT*>(&label), 2);
        RECT unit{0, 0, 70, 14}; // column width and row pitch in DU
        ::MapDialogRect(tab, &unit);
        RECT box{0, 0, 66, 10};
        ::MapDialogRect(tab, &box);
        const auto font = static_cast<WPARAM>(::SendMessageW(tab, WM_GETFONT, 0, 0));
        const DWORD drives = GetLogicalDrives();
        int slot = 0;
        for (int letter = 0; letter < 26; ++letter) {
            if ((drives & (1u << letter)) == 0) continue;
            const wchar_t root[] = {static_cast<wchar_t>(L'A' + letter), L':', L'\\', L'\0'};
            std::wstring text{root[0], L':'};
            if (const wchar_t* type = drive_type_name(GetDriveTypeW(root)); *type != L'\0') {
                text += L"  (";
                text += type;
                text += L")";
            }
            const int x = label.left + (slot % 4) * unit.right;
            const int y = label.bottom + MulDiv(unit.bottom, 1, 2) + (slot / 4) * unit.bottom;
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
    }

    void on_swatch(UINT, int, CWindow) {
        COLORREF colour = parse_hex(get_text(m_hWnd, IDC_LINE_HEX), settings::current().line_colour);
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
        draw_swatch(*item, parse_hex(get_text(m_hWnd, IDC_LINE_HEX), settings::current().line_colour));
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
    }

    [[nodiscard]] PageState from_controls() const {
        const HWND page = m_hWnd;
        PageState state = stored_state();
        settings::Settings& s = state.settings;
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

        s.lines = static_cast<settings::TreeLines>(get_combo(page, IDC_LINES, 0));
        s.line_thickness = get_int(page, IDC_LINE_THICKNESS, 1, 4, s.line_thickness);
        s.line_custom_colour = get_check(page, IDC_LINE_CUSTOM);
        s.line_opacity = get_int(page, IDC_LINE_OPACITY, 10, 100, s.line_opacity);
        s.line_colour = parse_hex(get_text(page, IDC_LINE_HEX), s.line_colour);
        s.row_padding = get_int(page, IDC_ROW_PADDING, 0, 12, s.row_padding);
        s.extensions = static_cast<settings::Extensions>(get_combo(page, IDC_EXTENSIONS, 0));
        s.sort.field = static_cast<model::SortField>(get_combo(page, IDC_SORT_FIELD, 0));
        s.sort.folders_first = get_check(page, IDC_FOLDERS_FIRST);
        s.sort.reverse = get_check(page, IDC_SORT_REVERSE);

        s.files = static_cast<fs::FileMode>(get_combo(page, IDC_FILES, 1));
        s.show_hidden = get_check(page, IDC_SHOW_HIDDEN);
        s.show_system = get_check(page, IDC_SHOW_SYSTEM);
        s.always_show = get_text(page, IDC_ALWAYS_SHOW);
        s.never_show = get_text(page, IDC_NEVER_SHOW);
        s.hide_patterns = get_text(page, IDC_HIDE_PATTERNS);
        s.sanitize();
        return state;
    }

    void to_controls(const PageState& state) {
        updating_ = true;
        const HWND page = m_hWnd;
        const settings::Settings& s = state.settings;
        for (int letter = 0; letter < 26; ++letter) {
            set_check(page, IDC_DRIVE_FIRST + letter, (s.hidden_drives & (1u << letter)) == 0);
        }
        set_text(page, IDC_TEMP_PLAYLIST, state.temp_playlist);
        set_check(page, IDC_RECURSIVE, state.recursive);

        set_combo(page, IDC_LINES, static_cast<int>(s.lines));
        set_int(page, IDC_LINE_THICKNESS, s.line_thickness);
        set_check(page, IDC_LINE_FOLLOW, !s.line_custom_colour);
        set_check(page, IDC_LINE_CUSTOM, s.line_custom_colour);
        set_int(page, IDC_LINE_OPACITY, s.line_opacity);
        set_text(page, IDC_LINE_HEX, format_hex(s.line_colour));
        set_int(page, IDC_ROW_PADDING, s.row_padding);
        set_combo(page, IDC_EXTENSIONS, static_cast<int>(s.extensions));
        set_combo(page, IDC_SORT_FIELD, static_cast<int>(s.sort.field));
        set_check(page, IDC_FOLDERS_FIRST, s.sort.folders_first);
        set_check(page, IDC_SORT_REVERSE, s.sort.reverse);

        set_combo(page, IDC_FILES, static_cast<int>(s.files));
        set_check(page, IDC_SHOW_HIDDEN, s.show_hidden);
        set_check(page, IDC_SHOW_SYSTEM, s.show_system);
        set_text(page, IDC_ALWAYS_SHOW, s.always_show);
        set_text(page, IDC_NEVER_SHOW, s.never_show);
        set_text(page, IDC_HIDE_PATTERNS, s.hide_patterns);
        ::InvalidateRect(find_control(page, IDC_LINE_SWATCH), nullptr, FALSE);
        updating_ = false;
        update_enabled();
    }

    const preferences_page_callback::ptr callback_;
    std::array<HWND, tab_count> tabs_{};
    bool initialised_{false};
    bool updating_{false};
    // A member: it hooks this dialog and its controls for the lifetime of both.
    fb2k::CDarkModeHooks dark_;
};

class PreferencesPageFactory : public preferences_page_impl<PreferencesPage> {
public:
    const char* get_name() override { return FILETREE_NAME; }
    GUID get_guid() override { return guids::preferences_page; }
    GUID get_parent_guid() override { return preferences_page::guid_display; }
};

preferences_page_factory_t<PreferencesPageFactory> g_preferences_page_factory;

} // namespace
} // namespace filetree::prefs
