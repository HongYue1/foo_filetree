#include <helpers/foobar2000+atl.h>

#include "panel.h"

#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>

#pragma comment(lib, "uxtheme.lib")

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif

namespace filetree::view {
namespace {

constexpr wchar_t tree_class[] = L"foo_filetree_tree";
constexpr DWORD history_dwell_ms = 800;
constexpr std::size_t history_limit = 100;

bool same_path(const std::wstring& a, const std::wstring& b) noexcept {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

} // namespace

void Panel::start(const settings::PanelState& saved) noexcept {
    try {
        const settings::Settings& s = settings::current();
        switch (s.startup) {
        case settings::Startup::restore: tree_.restore_state(saved); break;
        case settings::Startup::folder: tree_.navigate_to(s.startup_folder, true); break;
        case settings::Startup::collapsed: break;
        case settings::Startup::last_played:
            if (const std::wstring track = now_playing::last_played(); !track.empty()) {
                tree_.navigate_to(track, false);
            }
            break;
        }
    } catch (...) {
    }
}

void Panel::attach(HWND host, HostHooks hooks) noexcept {
    host_ = host;
    hooks_ = std::move(hooks);
    show_address_ = settings::current().show_address_bar;
    filter_mode_ = settings::current().filter_box;
    show_status_ = settings::current().show_status_bar;
    settings::subscribe(this);

    static const ATOM atom = [] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = TreeView::class_styles;
        wc.lpfnWndProc = &Panel::tree_proc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = tree_class;
        return RegisterClassExW(&wc);
    }();
    if (atom == 0) return;

    tree_.set_selection_listener([this] { on_selection(); });
    address_.create(host, AddressBar::Hooks{
                              [this](std::uint32_t node) { tree_.select_node(node); },
                              [this](const std::wstring& path) {
                                  // Like Explorer: going somewhere ends the filter.
                                  filter_.clear();
                                  close_floating_filter();
                                  return tree_.navigate_to(path, true);
                              },
                              [this](AddressBar::Button button) {
                                  if (button == AddressBar::back) go_back();
                                  if (button == AddressBar::forward) go_forward();
                                  if (button == AddressBar::up) tree_.select_parent();
                                  if (button == AddressBar::favourites) show_favourites();
                              },
                              [this] {
                                  if (tree_wnd_ != nullptr) SetFocus(tree_wnd_);
                              }});
    filter_.create(host, FilterBox::Hooks{
                             [this](const std::wstring& text) { tree_.set_filter(text); },
                             [this](bool cleared) {
                                 if (filter_mode_ == settings::FilterBox::floating &&
                                     (cleared || !filter_.active())) {
                                     close_floating_filter();
                                 }
                                 if (tree_wnd_ != nullptr) SetFocus(tree_wnd_);
                             },
                             [this] {
                                 if (filter_mode_ == settings::FilterBox::floating &&
                                     !filter_.active()) {
                                     close_floating_filter();
                                 }
                             }});
    search_.create(host, FilterBox::Hooks{
                             nullptr,
                             [this](bool) {
                                 if (tree_wnd_ != nullptr) SetFocus(tree_wnd_); // Down
                             },
                             [this] {
                                 if (!tree_.searching() && search_.text().empty()) close_search();
                             },
                             [this] {
                                 filter_.clear();
                                 close_floating_filter();
                                 if (!tree_.start_search(search_.text())) close_search();
                             },
                             [this] { on_search_escape(); }});
    search_.set_floating(true);
    search_.set_cue(L"Search every root (Enter)");
    tree_.set_search_listener([this] {
        if (!tree_.searching()) {
            if (!search_.has_focus()) close_search();
            search_.set_note({});
            return;
        }
        search_.set_note(tree_.search_status());
    });
    status_.create(host, [this] { return tree_.counters_text(); });
    status_.set_counters_enabled(settings::current().status_counters);
    transparent_ = settings::current().transparent;
    status_.set_transparent(transparent_);
    address_.set_transparent(transparent_);
    address_.show_favourites_button(!settings::current().quick_favourites.empty());
    // WM_CREATE attaches the tree (it needs the window).
    tree_wnd_ = CreateWindowExW(0, tree_class, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
                                    TreeView::window_styles,
                                0, 0,
                                0, 0, host, nullptr, core_api::get_my_instance(), this);
    layout();
}

void Panel::detach() noexcept {
    if (host_ == nullptr) return;
    settings::unsubscribe(this);
    if (tree_wnd_ != nullptr) {
        // The window outlives this call by a little (the host's children are destroyed after its
        // WM_DESTROY); it must not reach the detached view.
        SetWindowLongPtrW(tree_wnd_, GWLP_USERDATA, 0);
        tree_.detach();
        DestroyWindow(tree_wnd_);
        tree_wnd_ = nullptr;
    }
    tree_.set_selection_listener(nullptr);
    tree_.set_search_listener(nullptr);
    search_.destroy();
    filter_.destroy();
    status_.destroy();
    address_.destroy();
    history_.clear();
    history_at_ = 0;
    status_valid_ = false;
    host_ = nullptr;
}

void Panel::set_colours(const ViewColours& colours) noexcept {
    if (colours.dark != dark_ || !theme_applied_) {
        // The tree's scroll bar follows its window's theme.
        if (tree_wnd_ != nullptr) {
            SetWindowTheme(tree_wnd_, colours.dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        }
        dark_ = colours.dark;
        theme_applied_ = true;
    }
    tree_.set_colours(colours);
    address_.set_colours(colours);
    filter_.set_colours(colours);
    search_.set_colours(colours);
    status_.set_colours(colours);
}

void Panel::set_font(const LOGFONTW& font) noexcept {
    tree_.set_font(font);
    address_.set_font(font);
    filter_.set_font(font);
    search_.set_font(font);
    status_.set_font(font);
    layout();
}

void Panel::layout() noexcept {
    if (host_ == nullptr) return;
    RECT client{};
    GetClientRect(host_, &client);
    const bool in_bar = filter_mode_ == settings::FilterBox::bar;
    address_.set_parts(show_address_, in_bar ? filter_.height() : 0);
    const int bar =
        (show_address_ || in_bar) && address_.wnd() != nullptr ? address_.height() : 0;
    if (address_.wnd() != nullptr) {
        SetWindowPos(address_.wnd(), nullptr, 0, 0, client.right, bar,
                     SWP_NOZORDER | SWP_NOACTIVATE | (bar > 0 ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    const int status = show_status_ && status_.wnd() != nullptr
                           ? std::min<int>(status_.height(), std::max<int>(client.bottom - bar, 0))
                           : 0;
    if (status_.wnd() != nullptr) {
        SetWindowPos(status_.wnd(), nullptr, 0, client.bottom - status, client.right, status,
                     SWP_NOZORDER | SWP_NOACTIVATE |
                         (status > 0 ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    if (tree_wnd_ != nullptr) {
        SetWindowPos(tree_wnd_, nullptr, 0, bar, client.right,
                     std::max<int>(client.bottom - bar - status, 0), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (status > 0) update_status();
    place_filter();
    place_search();
}

void Panel::place_filter() noexcept {
    HWND box = filter_.wnd();
    if (box == nullptr || host_ == nullptr) return;
    filter_.set_floating(filter_mode_ == settings::FilterBox::floating);
    if (filter_mode_ == settings::FilterBox::bar && address_.wnd() != nullptr) {
        if (GetParent(box) != address_.wnd()) SetParent(box, address_.wnd());
        const RECT rect = address_.filter_rect();
        SetWindowPos(box, HWND_TOP, rect.left, rect.top, rect.right - rect.left,
                     rect.bottom - rect.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return;
    }
    if (GetParent(box) != host_) SetParent(box, host_);
    if (filter_mode_ != settings::FilterBox::floating || !floating_open_) {
        ShowWindow(box, SW_HIDE);
        return;
    }
    const RECT rect = floating_rect(search_open_ ? 1 : 0, filter_.height());
    SetWindowPos(box, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

RECT Panel::floating_rect(int slot, int height) const noexcept {
    // Over the top right of the rows, clear of the scroll bar.
    RECT client{};
    GetClientRect(host_, &client);
    RECT tree{};
    if (tree_wnd_ != nullptr) {
        GetWindowRect(tree_wnd_, &tree);
        MapWindowPoints(nullptr, host_, reinterpret_cast<POINT*>(&tree), 2);
    }
    RECT inner{};
    if (tree_wnd_ != nullptr) GetClientRect(tree_wnd_, &inner);
    const int dpi = tree_.dpi();
    const int margin = MulDiv(8, dpi, 96);
    const int right = tree.left + inner.right - margin;
    const int width = std::clamp<int>(client.right * 4 / 10, MulDiv(140, dpi, 96),
                                      MulDiv(300, dpi, 96));
    const int left = std::max<int>(right - width, tree.left + margin);
    const int top = tree.top + margin + slot * (search_.height() + margin / 2);
    return {left, top, std::max<int>(right, left), top + height};
}

void Panel::place_search() noexcept {
    HWND box = search_.wnd();
    if (box == nullptr) return;
    if (!search_open_) {
        ShowWindow(box, SW_HIDE);
        return;
    }
    const RECT rect = floating_rect(0, search_.height());
    SetWindowPos(box, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Panel::open_search() noexcept {
    if (!settings::current().disk_search) return;
    if (!search_open_) {
        search_open_ = true;
        place_search();
        place_filter(); // a floating filter moves below
    }
    search_.focus();
}

void Panel::close_search() noexcept {
    tree_.end_search();
    if (!search_open_) return;
    const bool had_focus = search_.has_focus();
    search_open_ = false;
    search_.clear();
    search_.set_note({});
    place_search();
    place_filter();
    if (had_focus && tree_wnd_ != nullptr) SetFocus(tree_wnd_);
}

void Panel::on_search_escape() noexcept {
    if (tree_.search_running()) {
        tree_.cancel_search();
        return;
    }
    close_search();
    if (tree_wnd_ != nullptr) SetFocus(tree_wnd_);
}

void Panel::update_status() noexcept {
    if (!show_status_ || status_.wnd() == nullptr) return;
    const model::Summary summary = tree_.status_summary();
    if (summary == status_summary_ && status_valid_) return;
    status_summary_ = summary;
    status_valid_ = true;
    try {
        status_.set_text(model::format_summary(summary));
    } catch (...) {
    }
}

void Panel::open_filter() noexcept {
    if (filter_mode_ == settings::FilterBox::off) return;
    if (filter_mode_ == settings::FilterBox::floating && !floating_open_) {
        floating_open_ = true;
        place_filter();
    }
    filter_.focus();
}

void Panel::close_floating_filter() noexcept {
    if (!floating_open_) return;
    floating_open_ = false;
    place_filter();
}

void Panel::on_settings_changed(std::uint32_t changes) noexcept {
    if ((changes & settings::change_layout) == 0) return;
    show_address_ = settings::current().show_address_bar;
    show_status_ = settings::current().show_status_bar;
    status_.set_counters_enabled(settings::current().status_counters);
    transparent_ = settings::current().transparent;
    status_.set_transparent(transparent_);
    address_.set_transparent(transparent_);
    address_.show_favourites_button(!settings::current().quick_favourites.empty());
    const settings::FilterBox mode = settings::current().filter_box;
    if (mode != filter_mode_) {
        filter_.clear();
        floating_open_ = false;
        filter_mode_ = mode;
    }
    if (!settings::current().disk_search) close_search();
    layout();
}

void Panel::on_selection() noexcept {
    try {
        tree_.selection_crumbs(crumbs_);
        std::wstring path = tree_.selected_path();
        if (!path.empty()) {
            const DWORD now = GetTickCount();
            if (history_.empty()) {
                history_.push_back(path);
                history_at_ = 0;
            } else if (!same_path(history_[history_at_], path)) {
                if (now - history_time_ >= history_dwell_ms) {
                    history_.resize(history_at_ + 1);
                    history_.push_back(path);
                    if (history_.size() > history_limit) history_.erase(history_.begin());
                    history_at_ = history_.size() - 1;
                } else {
                    history_[history_at_] = path;
                }
            }
            history_time_ = now;
        }
        address_.set_crumbs(crumbs_, std::move(path));
    } catch (...) {
    }
    update_buttons();
}

void Panel::show_favourites() noexcept {
    try {
        const std::vector<std::wstring> list = settings::current().quick_favourites;
        if (list.empty()) return;
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) return;
        for (std::size_t i = 0; i < list.size(); ++i) {
            std::wstring label;
            for (const wchar_t c : list[i]) {
                if (c == L'&') label.push_back(L'&'); // not an accelerator
                label.push_back(c);
            }
            AppendMenuW(menu, MF_STRING, i + 1, label.c_str());
        }
        const RECT button = address_.button_screen_rect(AddressBar::favourites);
        TPMPARAMS params{sizeof(params), button};
        const UINT chosen = static_cast<UINT>(TrackPopupMenuEx(
            menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL,
            button.left, button.bottom, address_.wnd(), &params));
        DestroyMenu(menu);
        if (chosen == 0 || chosen > list.size()) return;
        filter_.clear();
        close_floating_filter();
        if (!tree_.go_to_favourite(list[chosen - 1])) MessageBeep(MB_ICONWARNING);
        if (tree_wnd_ != nullptr) SetFocus(tree_wnd_);
    } catch (...) {
    }
}

void Panel::update_buttons() noexcept {
    address_.set_enabled(history_at_ > 0, history_at_ + 1 < history_.size(), crumbs_.size() > 1);
}

void Panel::go_back() noexcept {
    if (history_at_ == 0) return;
    --history_at_;
    go_to_history();
}

void Panel::go_forward() noexcept {
    if (history_at_ + 1 >= history_.size()) return;
    ++history_at_;
    go_to_history();
}

void Panel::go_to_history() noexcept {
    // The selection this causes matches history_[history_at_], so on_selection records nothing.
    history_time_ = 0;
    if (!tree_.navigate_to(history_[history_at_], false)) MessageBeep(MB_ICONWARNING);
    update_buttons();
}

bool Panel::on_panel_key(UINT msg, WPARAM key) noexcept {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    if (msg == WM_SYSKEYDOWN && !ctrl && !shift) {
        switch (key) {
        case VK_LEFT: go_back(); return true;
        case VK_RIGHT: go_forward(); return true;
        case VK_UP: tree_.select_parent(); return true;
        case VK_RETURN: tree_.show_selected_properties(); return true;
        default: return false;
        }
    }
    if (msg != WM_KEYDOWN) return false;
    switch (key) {
    case VK_ESCAPE:
        if (!filter_.active()) {
            if (!search_open_ && !tree_.searching()) return false;
            on_search_escape();
            return true;
        }
        filter_.clear();
        close_floating_filter();
        return true;
    case 'F':
        if (ctrl && shift && GetKeyState(VK_MENU) >= 0 && settings::current().disk_search) {
            open_search();
            return true;
        }
        if (!ctrl || shift || GetKeyState(VK_MENU) < 0 ||
            filter_mode_ == settings::FilterBox::off) {
            return false;
        }
        open_filter();
        return true;
    case VK_BROWSER_BACK: go_back(); return true;
    case VK_BROWSER_FORWARD: go_forward(); return true;
    case 'L':
        if (!ctrl || shift || GetKeyState(VK_MENU) < 0 || !show_address_) return false;
        address_.begin_edit();
        return true;
    default:
        return false;
    }
}

bool Panel::handle_message(HWND, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept {
    result = 0;
    switch (msg) {
    case WM_SIZE:
        layout();
        return true;
    case WM_SETFOCUS:
        if (tree_wnd_ != nullptr) SetFocus(tree_wnd_);
        return true;
    case WM_ERASEBKGND:
    case WM_PRINTCLIENT:
        // A transparent child (DrawThemeParentBackground) asks for what is behind it: pass the
        // request on to the host's parent, with the DC already offset to our client area.
        if (transparent_ && wp != 0) {
            DrawThemeParentBackground(host_, reinterpret_cast<HDC>(wp), nullptr);
        }
        result = 1;
        return true;
    case WM_DPICHANGED_AFTERPARENT:
        address_.refresh_dpi();
        filter_.refresh_dpi();
        search_.refresh_dpi();
        status_.refresh_dpi();
        layout();
        return true;
    case WM_SETTINGCHANGE:
        // Top-level only; the tree reads the wheel settings from it.
        if (tree_wnd_ != nullptr) SendMessageW(tree_wnd_, msg, wp, lp);
        return false;
    default:
        return false;
    }
}

LRESULT CALLBACK Panel::tree_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (panel == nullptr) return DefWindowProcW(wnd, msg, wp, lp);
    return panel->on_tree_message(wnd, msg, wp, lp);
}

LRESULT Panel::on_tree_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    LRESULT result = 0;
    switch (msg) {
    case WM_CREATE:
        tree_.attach(wnd);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (on_panel_key(msg, wp)) return 0;
        if (msg == WM_KEYDOWN && tree_.handle_message(wnd, msg, wp, lp, result)) return result;
        try {
            if (hooks_.shortcut && hooks_.shortcut(wp)) return 0;
        } catch (...) {
        }
        break;
    case WM_XBUTTONUP:
        if (GET_XBUTTON_WPARAM(wp) == XBUTTON1) go_back();
        if (GET_XBUTTON_WPARAM(wp) == XBUTTON2) go_forward();
        return TRUE;
    case WM_CONTEXTMENU:
        // In layout-edit mode DefWindowProc passes it up to the host, then to Default UI.
        if (hooks_.layout_editing && hooks_.layout_editing()) break;
        tree_.handle_message(wnd, msg, wp, lp, result);
        return result;
    default:
        if (tree_.handle_message(wnd, msg, wp, lp, result)) {
            if (msg == WM_PAINT) update_status();
            return result;
        }
        break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace filetree::view
