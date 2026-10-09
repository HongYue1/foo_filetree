#include <helpers/foobar2000+atl.h>

#include "address_bar.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>

#include "../platform/dpi.h"
#include "edit_util.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace filetree::view {
namespace {

constexpr wchar_t class_name[] = L"foo_filetree_address_bar";
constexpr UINT_PTR edit_subclass_id = 1;

constexpr wchar_t placeholder[] = L"Type a path (Ctrl+L)";
constexpr wchar_t overflow_text[] = L"\u2026";

void fill(HDC dc, const RECT& rect, COLORREF colour) noexcept {
    SetDCBrushColor(dc, colour);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

//! A chevron pointing left, right or up, centred on (cx, cy), `size` pixels across.
void draw_chevron(HDC dc, int cx, int cy, int size, int direction, int width,
                  COLORREF colour) noexcept {
    const int h = std::max(size / 2, 2);
    const int q = std::max(size / 4, 1);
    POINT points[3];
    switch (direction) {
    case 0: points[0] = {cx + q, cy - h}; points[1] = {cx - q, cy}; points[2] = {cx + q, cy + h}; break;
    case 1: points[0] = {cx - q, cy - h}; points[1] = {cx + q, cy}; points[2] = {cx - q, cy + h}; break;
    default: points[0] = {cx - h, cy + q}; points[1] = {cx, cy - q}; points[2] = {cx + h, cy + q}; break;
    }
    HPEN pen = CreatePen(PS_SOLID, width, colour);
    const HGDIOBJ old = SelectObject(dc, pen);
    Polyline(dc, points, 3);
    SelectObject(dc, old);
    DeleteObject(pen);
}

} // namespace

bool AddressBar::create(HWND parent, Hooks hooks) noexcept {
    if (wnd_ != nullptr) return true;
    hooks_ = std::move(hooks);
    static const ATOM atom = [] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &AddressBar::wnd_proc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = class_name;
        return RegisterClassExW(&wc);
    }();
    if (atom == 0) return false;
    wnd_ = CreateWindowExW(0, class_name, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                           nullptr, core_api::get_my_instance(), nullptr);
    if (wnd_ == nullptr) return false;
    SetWindowLongPtrW(wnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    dpi_ = static_cast<int>(dpi::of_window(wnd_));
    rebuild_font();
    return true;
}

void AddressBar::set_parts(bool address, int filter_height) noexcept {
    if (address == show_address_ && filter_height == filter_height_) return;
    show_address_ = address;
    filter_height_ = filter_height;
    if (!address) end_edit(false);
    layout();
}

void AddressBar::destroy() noexcept {
    if (edit_ != nullptr) {
        HWND edit = edit_;
        edit_ = nullptr;
        DestroyWindow(edit);
    }
    if (wnd_ != nullptr) {
        SetWindowLongPtrW(wnd_, GWLP_USERDATA, 0);
        DestroyWindow(wnd_);
        wnd_ = nullptr;
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
}

void AddressBar::set_colours(const ViewColours& colours) noexcept {
    colours_ = colours;
    bar_background_ = blend(colours.background, colours.text, colours.dark ? 0.06 : 0.035);
    hover_background_ = blend(bar_background_, colours.selection_background,
                              colours.dark ? 0.30 : 0.18);
    dim_text_ = blend(colours.text, colours.background, 0.45);
    border_ = blend(colours.background, colours.text, 0.15);
    if (edit_ != nullptr) SetWindowTheme(edit_, colours.dark ? L"DarkMode_CFD" : nullptr, nullptr);

    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, TRUE);
}

void AddressBar::set_transparent(bool transparent) noexcept {
    if (transparent == transparent_) return;
    transparent_ = transparent;
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void AddressBar::set_font(const LOGFONTW& font) noexcept {
    base_font_ = font;
    has_base_font_ = true;
    rebuild_font();
}

void AddressBar::refresh_dpi() noexcept {
    if (wnd_ == nullptr) return;
    dpi_ = static_cast<int>(dpi::of_window(wnd_));
    rebuild_font();
}

void AddressBar::rebuild_font() noexcept {
    if (!has_base_font_) {
        SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(base_font_), &base_font_, 0);
    }
    LOGFONTW scaled = base_font_;
    scaled.lfHeight = MulDiv(base_font_.lfHeight, dpi_, dpi::system());
    if (font_ != nullptr) DeleteObject(font_);
    font_ = CreateFontIndirectW(&scaled);
    if (wnd_ != nullptr) {
        HDC dc = GetDC(wnd_);
        const HGDIOBJ old = SelectObject(dc, font_);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, old);
        ReleaseDC(wnd_, dc);
        text_height_ = metrics.tmHeight;
    }
    height_ = std::max(text_height_ + dpi::scale(10, dpi_), dpi::scale(24, dpi_));
    if (edit_ != nullptr) SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    layout();
}

void AddressBar::set_crumbs(std::vector<TreeView::Crumb> crumbs, std::wstring path) noexcept {
    crumbs_ = std::move(crumbs);
    path_ = std::move(path);
    hover_ = pressed_ = hit_none;
    layout();
}

void AddressBar::set_enabled(bool back_enabled, bool forward_enabled, bool up_enabled) noexcept {
    const bool changed = enabled_[back] != back_enabled || enabled_[forward] != forward_enabled ||
                         enabled_[up] != up_enabled;
    enabled_[back] = back_enabled;
    enabled_[forward] = forward_enabled;
    enabled_[up] = up_enabled;
    if (changed && wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void AddressBar::layout() noexcept {
    crumb_rects_.assign(crumbs_.size(), RECT{});
    overflow_rect_ = {};
    if (wnd_ == nullptr) return;
    RECT client{};
    GetClientRect(wnd_, &client);
    const int pad = dpi::scale(6, dpi_);
    const int separator = dpi::scale(12, dpi_);
    const int left = height_ * button_count + dpi::scale(4, dpi_);
    int right = client.right - pad;

    // The filter box: on the right, or the whole bar when the address part is hidden.
    filter_rect_ = {};
    if (filter_height_ > 0) {
        const int margin = dpi::scale(4, dpi_);
        const int width = show_address_ ? std::clamp<int>(client.right * 3 / 10, dpi::scale(120, dpi_),
                                                          dpi::scale(260, dpi_))
                                        : client.right - 2 * margin;
        const int box = std::min(filter_height_, height_ - 3);
        const int top = (height_ - 1 - box) / 2;
        filter_rect_ = {std::max<int>(client.right - margin - width, 0), top,
                        client.right - margin, top + box};
        right = filter_rect_.left - dpi::scale(8, dpi_);
    }
    if (!show_address_) {
        InvalidateRect(wnd_, nullptr, FALSE);
        return;
    }

    HDC dc = GetDC(wnd_);
    const HGDIOBJ old = SelectObject(dc, font_);
    std::vector<int> widths(crumbs_.size());
    int total = 0;
    for (std::size_t i = 0; i < crumbs_.size(); ++i) {
        SIZE extent{};
        GetTextExtentPoint32W(dc, crumbs_[i].name.c_str(), static_cast<int>(crumbs_[i].name.size()),
                              &extent);
        widths[i] = extent.cx + 2 * pad;
        total += widths[i] + (i > 0 ? separator : 0);
    }
    SIZE overflow{};
    GetTextExtentPoint32W(dc, overflow_text, 1, &overflow);
    SelectObject(dc, old);
    ReleaseDC(wnd_, dc);

    // Too wide: drop crumbs from the left behind a "..." (the last one always stays).
    std::size_t first = 0;
    const int overflow_width = overflow.cx + 2 * pad + separator;
    while (first + 1 < crumbs_.size() && left + total + (first > 0 ? overflow_width : 0) > right) {
        total -= widths[first] + separator;
        ++first;
    }
    int x = left;
    if (first > 0) {
        overflow_rect_ = {x, 0, x + overflow.cx + 2 * pad, height_};
        x = overflow_rect_.right + separator;
    }
    for (std::size_t i = first; i < crumbs_.size(); ++i) {
        crumb_rects_[i] = {x, 0, std::min(x + widths[i], right), height_};
        x += widths[i] + separator;
    }
    InvalidateRect(wnd_, nullptr, FALSE);
}

int AddressBar::hit(int x, int y) const noexcept {
    if (y < 0 || y >= height_ || !show_address_) return hit_none;
    const POINT at{x, y};
    if (PtInRect(&filter_rect_, at)) return hit_none;
    if (x >= 0 && x < height_ * button_count) return x / height_;
    const POINT point{x, y};
    if (PtInRect(&overflow_rect_, point)) return hit_overflow;
    for (std::size_t i = 0; i < crumb_rects_.size(); ++i) {
        if (PtInRect(&crumb_rects_[i], point)) return crumb_hit + static_cast<int>(i);
    }
    return hit_blank;
}

void AddressBar::paint(HDC dc, const RECT& client) noexcept {
    if (transparent_) {
        fill(dc, client, colours_.background); // fallback when the parent paints nothing
        DrawThemeParentBackground(wnd_, dc, &client);
    } else {
        fill(dc, client, bar_background_);
    }
    fill(dc, RECT{client.left, client.bottom - 1, client.right, client.bottom}, border_);
    if (!show_address_) return;
    const int pen = std::max(dpi::scale(3, dpi_) / 2, 1);
    const int glyph = dpi::scale(10, dpi_);
    for (int b = 0; b < button_count; ++b) {
        const RECT rect{b * height_, 0, (b + 1) * height_, height_ - 1};
        if (enabled_[b] && (hover_ == b || pressed_ == b)) fill(dc, rect, hover_background_);
        draw_chevron(dc, (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2, glyph, b, pen,
                     enabled_[b] ? colours_.text : dim_text_);
    }
    if (edit_ != nullptr) return; // the edit box covers the crumbs

    SetBkMode(dc, TRANSPARENT);
    const HGDIOBJ old_font = SelectObject(dc, font_);
    constexpr UINT format = DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
    const int separator = dpi::scale(12, dpi_);
    const int chevron = dpi::scale(7, dpi_);
    const int mid = height_ / 2;
    if (crumbs_.empty()) {
        RECT rect{height_ * button_count + dpi::scale(10, dpi_), 0, client.right, height_};
        SetTextColor(dc, dim_text_);
        DrawTextW(dc, placeholder, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    if (overflow_rect_.right > overflow_rect_.left) {
        if (hover_ == hit_overflow) fill(dc, overflow_rect_, hover_background_);
        RECT rect = overflow_rect_;
        SetTextColor(dc, colours_.text);
        DrawTextW(dc, overflow_text, 1, &rect, format);
        draw_chevron(dc, overflow_rect_.right + separator / 2, mid, chevron, 1, 1, dim_text_);
    }
    for (std::size_t i = 0; i < crumbs_.size(); ++i) {
        RECT rect = crumb_rects_[i];
        if (rect.right <= rect.left) continue;
        if (hover_ == crumb_hit + static_cast<int>(i)) {
            fill(dc, RECT{rect.left, rect.top, rect.right, rect.bottom - 1}, hover_background_);
        }
        SetTextColor(dc, colours_.text);
        DrawTextW(dc, crumbs_[i].name.c_str(), static_cast<int>(crumbs_[i].name.size()), &rect,
                  format);
        if (i + 1 < crumbs_.size()) {
            draw_chevron(dc, rect.right + separator / 2, mid, chevron, 1, 1, dim_text_);
        }
    }
    SelectObject(dc, old_font);
}

void AddressBar::set_hover(int code) noexcept {
    if (code == hover_) return;
    hover_ = code;
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void AddressBar::activate(int code) noexcept {
    try {
        if (code >= 0 && code < button_count) {
            if (enabled_[code] && hooks_.button) hooks_.button(static_cast<Button>(code));
        } else if (code >= crumb_hit) {
            const auto index = static_cast<std::size_t>(code - crumb_hit);
            if (index < crumbs_.size() && hooks_.crumb) hooks_.crumb(crumbs_[index].node);
        } else if (code == hit_blank || code == hit_overflow) {
            begin_edit();
        }
    } catch (...) {
    }
}

void AddressBar::begin_edit() noexcept {
    if (wnd_ == nullptr || edit_ != nullptr || !show_address_) return;
    RECT client{};
    GetClientRect(wnd_, &client);
    const int left = height_ * button_count + dpi::scale(4, dpi_);
    // One line high and centred: a single-line EDIT draws its text at the top.
    const int edit_height = text_height_;
    const int top = (height_ - 1 - edit_height) / 2;
    const int right = filter_rect_.right > filter_rect_.left ? filter_rect_.left - dpi::scale(8, dpi_)
                                                             : client.right - dpi::scale(4, dpi_);
    edit_ = CreateWindowExW(0, WC_EDITW, path_.c_str(), WS_CHILD | ES_AUTOHSCROLL | ES_LEFT, left,
                            top, std::max<int>(right - left, 10), edit_height, wnd_, nullptr,
                            core_api::get_my_instance(), nullptr);
    if (edit_ == nullptr) return;
    if (colours_.dark) SetWindowTheme(edit_, L"DarkMode_CFD", nullptr);
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    const int pad = dpi::scale(4, dpi_);
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(pad, pad));
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    SetWindowSubclass(edit_, edit_proc, edit_subclass_id, reinterpret_cast<DWORD_PTR>(this));
    ShowWindow(edit_, SW_SHOW);
    SetFocus(edit_);
    InvalidateRect(wnd_, nullptr, FALSE);
}

void AddressBar::end_edit(bool commit) noexcept {
    HWND edit = edit_;
    if (edit == nullptr) return;
    if (commit) {
        std::wstring text;
        const int length = GetWindowTextLengthW(edit);
        text.resize(static_cast<std::size_t>(std::max(length, 0)) + 1);
        text.resize(static_cast<std::size_t>(
            GetWindowTextW(edit, text.data(), static_cast<int>(text.size()))));
        bool ok = false;
        try {
            ok = hooks_.navigate && hooks_.navigate(text);
        } catch (...) {
        }
        if (!ok) {
            // Not a path any root holds: keep the text for correcting.
            MessageBeep(MB_ICONWARNING);
            SendMessageW(edit, EM_SETSEL, 0, -1);
            return;
        }
    }
    // Clear first: moving the focus sends WM_KILLFOCUS, which re-enters here.
    edit_ = nullptr;
    if (GetFocus() == edit) {
        try {
            if (hooks_.done) hooks_.done();
        } catch (...) {
        }
    }
    DestroyWindow(edit);
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

LRESULT CALLBACK AddressBar::edit_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR,
                                       DWORD_PTR data) noexcept {
    auto* bar = reinterpret_cast<AddressBar*>(data);
    if (edit::ctrl_backspace(wnd, msg, wp)) return 0;
    switch (msg) {
    case WM_GETDLGCODE:
        return DefSubclassProc(wnd, msg, wp, lp) | DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_ESCAPE) {
            bar->end_edit(wp == VK_RETURN);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == L'\r' || wp == 0x1b) return 0; // no beep
        break;
    case WM_KILLFOCUS:
        if (bar->edit_ == wnd) {
            // Clicked elsewhere: drop the edit, keep the crumbs.
            bar->edit_ = nullptr;
            DefSubclassProc(wnd, msg, wp, lp);
            PostMessageW(wnd, WM_CLOSE, 0, 0);
            if (bar->wnd_ != nullptr) InvalidateRect(bar->wnd_, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(wnd, edit_proc, edit_subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(wnd, msg, wp, lp);
}

LRESULT CALLBACK AddressBar::wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    auto* bar = reinterpret_cast<AddressBar*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (bar == nullptr) return DefWindowProcW(wnd, msg, wp, lp);
    return bar->on_message(wnd, msg, wp, lp);
}

LRESULT AddressBar::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_SIZE:
        layout();
        if (edit_ != nullptr) {
            RECT client{};
            GetClientRect(wnd, &client);
            RECT rect{};
            GetWindowRect(edit_, &rect);
            MapWindowPoints(nullptr, wnd, reinterpret_cast<POINT*>(&rect), 2);
            const int right = filter_rect_.right > filter_rect_.left
                                  ? filter_rect_.left - dpi::scale(8, dpi_)
                                  : client.right - dpi::scale(4, dpi_);
            SetWindowPos(edit_, nullptr, 0, 0, std::max<int>(right - rect.left, 10),
                         rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC target = BeginPaint(wnd, &ps);
        RECT client{};
        GetClientRect(wnd, &client);
        // Small and rarely painted: a buffer per paint is fine here (unlike the tree).
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, std::max<int>(client.right, 1),
                                                std::max<int>(client.bottom, 1));
        if (dc != nullptr && bitmap != nullptr) {
            const HGDIOBJ old = SelectObject(dc, bitmap);
            paint(dc, client);
            BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, old);
        } else {
            paint(target, client);
        }
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (dc != nullptr) DeleteDC(dc);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!tracking_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, wnd, 0};
            tracking_ = TrackMouseEvent(&track) != FALSE;
        }
        set_hover(hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
        return 0;
    case WM_MOUSELEAVE:
        tracking_ = false;
        set_hover(hit_none);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        pressed_ = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        SetCapture(wnd);
        InvalidateRect(wnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const int code = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        const int pressed = pressed_;
        pressed_ = hit_none;
        if (GetCapture() == wnd) ReleaseCapture();
        InvalidateRect(wnd, nullptr, FALSE);
        if (code == pressed && code != hit_none) activate(code);
        return 0;
    }
    case WM_CAPTURECHANGED:
        pressed_ = hit_none;
        return 0;
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lp) == edit_) {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, colours_.text);
            SetBkColor(dc, colours_.background);
            SetDCBrushColor(dc, colours_.background);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace filetree::view
