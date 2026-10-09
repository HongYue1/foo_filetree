#include <helpers/foobar2000+atl.h>

#include "status_bar.h"

#include <commctrl.h>

#include <algorithm>

#include "../platform/dpi.h"

#pragma comment(lib, "comctl32.lib")

namespace filetree::view {
namespace {

constexpr wchar_t class_name[] = L"foo_filetree_status_bar";

} // namespace

bool StatusBar::create(HWND parent, std::function<std::wstring()> counters) noexcept {
    if (wnd_ != nullptr) return true;
    counters_ = std::move(counters);
    static const ATOM atom = [] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &StatusBar::wnd_proc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = class_name;
        return RegisterClassExW(&wc);
    }();
    if (atom == 0) return false;
    wnd_ = CreateWindowExW(0, class_name, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent,
                           nullptr, core_api::get_my_instance(), nullptr);
    if (wnd_ == nullptr) return false;
    SetWindowLongPtrW(wnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    dpi_ = static_cast<int>(dpi::of_window(wnd_));
    rebuild_font();
    return true;
}

void StatusBar::destroy() noexcept {
    tooltip_.destroy();
    if (wnd_ != nullptr) {
        SetWindowLongPtrW(wnd_, GWLP_USERDATA, 0);
        DestroyWindow(wnd_);
        wnd_ = nullptr;
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    text_.clear();
}

void StatusBar::set_colours(const ViewColours& colours) noexcept {
    colours_ = colours;
    text_colour_ = blend(colours.background, colours.text, 0.75);
    line_colour_ = blend(colours.background, colours.text, 0.15);
    tooltip_.set_dark(colours.dark);
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void StatusBar::set_font(const LOGFONTW& font) noexcept {
    base_font_ = font;
    has_base_font_ = true;
    rebuild_font();
}

void StatusBar::refresh_dpi() noexcept {
    if (wnd_ == nullptr) return;
    dpi_ = static_cast<int>(dpi::of_window(wnd_));
    rebuild_font();
}

void StatusBar::rebuild_font() noexcept {
    if (!has_base_font_) {
        SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(base_font_), &base_font_, 0);
    }
    LOGFONTW scaled = base_font_;
    scaled.lfHeight = MulDiv(base_font_.lfHeight, dpi_, dpi::system());
    if (font_ != nullptr) DeleteObject(font_);
    font_ = CreateFontIndirectW(&scaled);
    int text_height = 16;
    if (wnd_ != nullptr) {
        HDC dc = GetDC(wnd_);
        const HGDIOBJ old = SelectObject(dc, font_);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, old);
        ReleaseDC(wnd_, dc);
        text_height = metrics.tmHeight;
        InvalidateRect(wnd_, nullptr, FALSE);
    }
    height_ = text_height + dpi::scale(6, dpi_) + 1;
    tooltip_.set_font(font_);
}

void StatusBar::set_text(std::wstring text) noexcept {
    if (text == text_) return;
    text_ = std::move(text);
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void StatusBar::paint(HDC dc) noexcept {
    RECT client{};
    GetClientRect(wnd_, &client);
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, colours_.background);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const RECT line{client.left, client.top, client.right, client.top + 1};
    SetDCBrushColor(dc, line_colour_);
    FillRect(dc, &line, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if (text_.empty()) return;
    const HGDIOBJ old = SelectObject(dc, font_ != nullptr ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text_colour_);
    const int pad = dpi::scale(6, dpi_);
    RECT text{client.left + pad, client.top + 1, client.right - pad, client.bottom};
    DrawTextW(dc, text_.c_str(), static_cast<int>(text_.size()), &text,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

LRESULT CALLBACK StatusBar::wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    auto* bar = reinterpret_cast<StatusBar*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (bar == nullptr) return DefWindowProcW(wnd, msg, wp, lp);
    return bar->on_message(wnd, msg, wp, lp);
}

LRESULT StatusBar::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        paint(dc);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_SIZE: {
        RECT client{};
        GetClientRect(wnd, &client);
        if (tooltip_.ensure(wnd)) {
            tooltip_.set_dark(colours_.dark);
            tooltip_.set_font(font_);
            tooltip_.set_area(client);
        }
        InvalidateRect(wnd, nullptr, FALSE);
        return 0;
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lp);
        if (header != nullptr && header->hwndFrom == tooltip_.wnd() &&
            header->code == TTN_GETDISPINFOW) {
            auto* info = reinterpret_cast<NMTTDISPINFOW*>(lp);
            try {
                tip_text_ = counters_ ? counters_() : std::wstring{};
            } catch (...) {
                tip_text_.clear();
            }
            info->lpszText = tip_text_.data();
            return 0;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace filetree::view
