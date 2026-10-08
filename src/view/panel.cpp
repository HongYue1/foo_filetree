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

void Panel::attach(HWND host, HostHooks hooks) noexcept {
    host_ = host;
    hooks_ = std::move(hooks);
    show_address_ = settings::current().show_address_bar;
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
                                  return tree_.navigate_to(path, true);
                              },
                              [this](AddressBar::Button button) {
                                  if (button == AddressBar::back) go_back();
                                  if (button == AddressBar::forward) go_forward();
                                  if (button == AddressBar::up) tree_.select_parent();
                              },
                              [this] {
                                  if (tree_wnd_ != nullptr) SetFocus(tree_wnd_);
                              }});
    // WM_CREATE attaches the tree (it needs the window).
    tree_wnd_ = CreateWindowExW(0, tree_class, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TreeView::window_styles, 0, 0,
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
    address_.destroy();
    history_.clear();
    history_at_ = 0;
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
}

void Panel::set_font(const LOGFONTW& font) noexcept {
    tree_.set_font(font);
    address_.set_font(font);
    layout();
}

void Panel::layout() noexcept {
    if (host_ == nullptr) return;
    RECT client{};
    GetClientRect(host_, &client);
    const int bar = show_address_ && address_.wnd() != nullptr ? address_.height() : 0;
    if (address_.wnd() != nullptr) {
        SetWindowPos(address_.wnd(), nullptr, 0, 0, client.right, bar,
                     SWP_NOZORDER | SWP_NOACTIVATE | (bar > 0 ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    if (tree_wnd_ != nullptr) {
        SetWindowPos(tree_wnd_, nullptr, 0, bar, client.right, std::max<int>(client.bottom - bar, 0),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void Panel::on_settings_changed(std::uint32_t changes) noexcept {
    if ((changes & settings::change_layout) == 0) return;
    show_address_ = settings::current().show_address_bar;
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
        default: return false;
        }
    }
    if (msg != WM_KEYDOWN) return false;
    switch (key) {
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
        result = 1;
        return true;
    case WM_DPICHANGED_AFTERPARENT:
        address_.refresh_dpi();
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
        if (tree_.handle_message(wnd, msg, wp, lp, result)) return result;
        break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace filetree::view
