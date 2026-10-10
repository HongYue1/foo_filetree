#include <helpers/foobar2000+atl.h>

#include "filter_box.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>

#include "../platform/dpi.h"
#include "edit_util.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace filetree::view {
namespace {

constexpr wchar_t class_name[] = L"foo_filetree_filter_box";
constexpr UINT_PTR subclass_id = 1;
constexpr wchar_t cue[] = L"Filter (Ctrl+F)";

} // namespace

bool FilterBox::create(HWND parent, Hooks hooks) noexcept {
    if (wnd_ != nullptr) return true;
    hooks_ = std::move(hooks);
    static const ATOM atom = [] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &FilterBox::wnd_proc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        wc.lpszClassName = class_name;
        return RegisterClassExW(&wc);
    }();
    if (atom == 0) return false;
    wnd_ = CreateWindowExW(0, class_name, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                           0, 0, parent, nullptr, core_api::get_my_instance(), nullptr);
    if (wnd_ == nullptr) return false;
    SetWindowLongPtrW(wnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    edit_ = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0,
                            0, 0, 0, wnd_, nullptr, core_api::get_my_instance(), nullptr);
    if (edit_ != nullptr) {
        SendMessageW(edit_, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(cue));
        SetWindowSubclass(edit_, edit_proc, subclass_id, reinterpret_cast<DWORD_PTR>(this));
    }
    dpi_ = static_cast<int>(dpi::of_window(wnd_));
    rebuild_font();
    return true;
}

void FilterBox::destroy() noexcept {
    if (wnd_ != nullptr) {
        SetWindowLongPtrW(wnd_, GWLP_USERDATA, 0);
        DestroyWindow(wnd_);
        wnd_ = nullptr;
        edit_ = nullptr; // a child: destroyed with the frame
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
}

void FilterBox::set_colours(const ViewColours& colours) noexcept {
    colours_ = colours;
    border_ = floating_ ? colours.selection_background
                        : blend(colours.background, colours.text, 0.15);
    if (edit_ != nullptr) {
        SetWindowTheme(edit_, colours.dark ? L"DarkMode_CFD" : nullptr, nullptr);
        InvalidateRect(edit_, nullptr, TRUE);
    }
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, TRUE);
}

void FilterBox::set_floating(bool floating) noexcept {
    floating_ = floating;
    set_colours(colours_);
}

void FilterBox::set_font(const LOGFONTW& font) noexcept {
    base_font_ = font;
    has_base_font_ = true;
    rebuild_font();
}

void FilterBox::refresh_dpi() noexcept {
    if (wnd_ == nullptr) return;
    dpi_ = static_cast<int>(dpi::of_window(wnd_));
    rebuild_font();
}

void FilterBox::rebuild_font() noexcept {
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
    height_ = text_height_ + dpi::scale(8, dpi_) + 2;
    if (edit_ != nullptr) {
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        const int pad = dpi::scale(5, dpi_);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(pad, pad));
    }
    place_edit();
}

void FilterBox::place_edit() noexcept {
    if (wnd_ == nullptr || edit_ == nullptr) return;
    // A single-line EDIT draws its text at the top: make it exactly one line high and centre it
    // in the frame, so the text and the cue banner sit in the middle.
    RECT client{};
    GetClientRect(wnd_, &client);
    const int top = std::max<int>((client.bottom - text_height_) / 2, 1);
    note_width_ = 0;
    if (!note_.empty() && font_ != nullptr) {
        HDC dc = GetDC(wnd_);
        const HGDIOBJ old = SelectObject(dc, font_);
        SIZE extent{};
        GetTextExtentPoint32W(dc, note_.c_str(), static_cast<int>(note_.size()), &extent);
        SelectObject(dc, old);
        ReleaseDC(wnd_, dc);
        note_width_ = std::min<int>(extent.cx + dpi::scale(10, dpi_), client.right / 2);
    }
    SetWindowPos(edit_, nullptr, 1, top, std::max<int>(client.right - 2 - note_width_, 0),
                 std::min<int>(text_height_, std::max<int>(client.bottom - 2, 0)),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void FilterBox::focus() noexcept {
    if (edit_ == nullptr) return;
    SetFocus(edit_);
    SendMessageW(edit_, EM_SETSEL, 0, -1);
}

std::wstring FilterBox::text() const {
    std::wstring out;
    if (edit_ == nullptr) return out;
    out.resize(static_cast<std::size_t>(std::max(GetWindowTextLengthW(edit_), 0)) + 1);
    out.resize(static_cast<std::size_t>(
        GetWindowTextW(edit_, out.data(), static_cast<int>(out.size()))));
    return out;
}

void FilterBox::set_cue(const wchar_t* text) noexcept {
    if (edit_ != nullptr) SendMessageW(edit_, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(text));
}

void FilterBox::set_note(std::wstring note) noexcept {
    if (note == note_) return;
    note_ = std::move(note);
    place_edit();
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void FilterBox::clear() noexcept {
    if (edit_ != nullptr && GetWindowTextLengthW(edit_) > 0) SetWindowTextW(edit_, L"");
}

LRESULT CALLBACK FilterBox::edit_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR,
                                      DWORD_PTR data) noexcept {
    auto* box = reinterpret_cast<FilterBox*>(data);
    if (edit::ctrl_backspace(wnd, msg, wp)) return 0;
    switch (msg) {
    case WM_GETDLGCODE:
        return DefSubclassProc(wnd, msg, wp, lp) | DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wp == VK_RETURN && box->hooks_.submitted) {
            try {
                box->hooks_.submitted();
            } catch (...) {
            }
            return 0;
        }
        if (wp == VK_ESCAPE && box->hooks_.escaped) {
            try {
                box->hooks_.escaped();
            } catch (...) {
            }
            return 0;
        }
        if (wp == VK_ESCAPE || wp == VK_RETURN || wp == VK_DOWN) {
            const bool cleared = wp == VK_ESCAPE && GetWindowTextLengthW(wnd) > 0;
            if (cleared) SetWindowTextW(wnd, L"");
            try {
                if (box->hooks_.done) box->hooks_.done(cleared);
            } catch (...) {
            }
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == L'\r' || wp == 0x1b) return 0; // no beep
        break;
    case WM_KILLFOCUS: {
        const LRESULT result = DefSubclassProc(wnd, msg, wp, lp);
        try {
            if (box->hooks_.blurred) box->hooks_.blurred();
        } catch (...) {
        }
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(wnd, edit_proc, subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(wnd, msg, wp, lp);
}

LRESULT CALLBACK FilterBox::wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    auto* box = reinterpret_cast<FilterBox*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (box == nullptr) return DefWindowProcW(wnd, msg, wp, lp);
    return box->on_message(wnd, msg, wp, lp);
}

LRESULT FilterBox::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_SIZE:
        place_edit();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT client{};
        GetClientRect(wnd, &client);
        SetDCBrushColor(dc, border_);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        RECT inner{client.left + 1, client.top + 1, client.right - 1, client.bottom - 1};
        SetDCBrushColor(dc, colours_.background);
        FillRect(dc, &inner, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if (note_width_ > 0) {
            RECT note{inner.right - note_width_, inner.top, inner.right - dpi::scale(5, dpi_),
                      inner.bottom};
            const HGDIOBJ old = SelectObject(dc, font_);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, blend(colours_.text, colours_.background, 0.45));
            DrawTextW(dc, note_.c_str(), static_cast<int>(note_.size()), &note,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(dc, old);
        }
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        focus(); // a click on the frame's padding
        return 0;
    case WM_COMMAND:
        if (reinterpret_cast<HWND>(lp) == edit_ && edit_ != nullptr && HIWORD(wp) == EN_CHANGE) {
            std::wstring text;
            const int length = GetWindowTextLengthW(edit_);
            text.resize(static_cast<std::size_t>(std::max(length, 0)) + 1);
            text.resize(static_cast<std::size_t>(
                GetWindowTextW(edit_, text.data(), static_cast<int>(text.size()))));
            try {
                if (hooks_.changed) hooks_.changed(text);
            } catch (...) {
            }
            return 0;
        }
        break;
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
