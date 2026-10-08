#include <helpers/foobar2000+atl.h>

#include "prefs_util.h"

#include <helpers/DarkMode.h>

#include <commdlg.h>

#include <algorithm>
#include <cwchar>

#pragma comment(lib, "comdlg32.lib")

namespace filetree::prefs {

HWND find_control(HWND page, int id) noexcept {
    if (HWND direct = GetDlgItem(page, id)) return direct;
    for (HWND w = GetWindow(page, GW_CHILD); w != nullptr; w = GetWindow(w, GW_HWNDNEXT)) {
        wchar_t name[16]{};
        GetClassNameW(w, name, 16);
        if (wcscmp(name, L"#32770") != 0) continue;
        if (HWND found = GetDlgItem(w, id)) return found;
    }
    return nullptr;
}

std::wstring get_text(HWND page, int id) {
    const HWND control = find_control(page, id);
    if (control == nullptr) return {};
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    text.resize(static_cast<std::size_t>(
        GetWindowTextW(control, text.data(), static_cast<int>(text.size()))));
    return text;
}

void set_text(HWND page, int id, const std::wstring& text) {
    if (const HWND control = find_control(page, id)) SetWindowTextW(control, text.c_str());
}

bool get_check(HWND page, int id) noexcept {
    const HWND control = find_control(page, id);
    return control != nullptr && SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void set_check(HWND page, int id, bool checked) noexcept {
    if (const HWND control = find_control(page, id)) {
        SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

int get_int(HWND page, int id, int low, int high, int fallback) noexcept {
    const HWND control = find_control(page, id);
    if (control == nullptr) return fallback;
    BOOL ok = FALSE;
    const UINT value = GetDlgItemInt(GetParent(control), id, &ok, FALSE);
    if (!ok) return fallback;
    return std::clamp(static_cast<int>(std::min<UINT>(value, 0x7fffffff)), low, high);
}

void set_int(HWND page, int id, int value) noexcept {
    if (const HWND control = find_control(page, id)) {
        SetDlgItemInt(GetParent(control), id, static_cast<UINT>(std::max(value, 0)), FALSE);
    }
}

void fill_combo(HWND page, int id, std::initializer_list<const wchar_t*> items) {
    const HWND combo = find_control(page, id);
    if (combo == nullptr) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* item : items) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }
}

int get_combo(HWND page, int id, int fallback) noexcept {
    const HWND combo = find_control(page, id);
    if (combo == nullptr) return fallback;
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return index == CB_ERR ? fallback : static_cast<int>(index);
}

void set_combo(HWND page, int id, int index) noexcept {
    if (const HWND combo = find_control(page, id)) {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    }
}

void enable(HWND page, int id, bool enabled) noexcept {
    if (const HWND control = find_control(page, id)) EnableWindow(control, enabled ? TRUE : FALSE);
}

COLORREF parse_hex(const std::wstring& text, COLORREF fallback) noexcept {
    std::wstring_view digits = text;
    if (!digits.empty() && digits.front() == L'#') digits.remove_prefix(1);
    if (digits.size() != 6) return fallback;
    unsigned value = 0;
    for (const wchar_t c : digits) {
        unsigned digit = 0;
        if (c >= L'0' && c <= L'9') {
            digit = static_cast<unsigned>(c - L'0');
        } else if (c >= L'a' && c <= L'f') {
            digit = static_cast<unsigned>(c - L'a' + 10);
        } else if (c >= L'A' && c <= L'F') {
            digit = static_cast<unsigned>(c - L'A' + 10);
        } else {
            return fallback;
        }
        value = (value << 4) | digit;
    }
    return RGB((value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff);
}

std::wstring format_hex(COLORREF colour) {
    wchar_t buffer[8]{};
    swprintf_s(buffer, L"%02X%02X%02X", GetRValue(colour), GetGValue(colour), GetBValue(colour));
    return buffer;
}

void pad_edits(HWND tab) noexcept {
    HDC screen = GetDC(tab);
    const int dpi = screen != nullptr ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen != nullptr) ReleaseDC(tab, screen);
    const int pad = MulDiv(4, dpi, 96);
    for (HWND w = GetWindow(tab, GW_CHILD); w != nullptr; w = GetWindow(w, GW_HWNDNEXT)) {
        wchar_t name[16]{};
        GetClassNameW(w, name, 16);
        if (_wcsicmp(name, L"Edit") == 0) {
            SendMessageW(w, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(pad, pad));
        }
    }
}

void draw_swatch(const DRAWITEMSTRUCT& item, COLORREF colour) noexcept {
    const HDC dc = item.hDC;
    const HWND tab = GetParent(item.hwndItem);
    // The tab's own background brush, right in light and dark mode.
    auto brush = reinterpret_cast<HBRUSH>(SendMessageW(
        tab, WM_CTLCOLORDLG, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(tab)));
    FillRect(dc, &item.rcItem, brush != nullptr ? brush : GetSysColorBrush(COLOR_BTNFACE));
    const bool dark = DarkMode::IsDialogDark(tab);
    RECT swatch = item.rcItem;
    InflateRect(&swatch, -1, -1);
    COLORREF fill = colour;
    if ((item.itemState & ODS_DISABLED) != 0) {
        const COLORREF back = dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE);
        fill = RGB((GetRValue(fill) + 2 * GetRValue(back)) / 3,
                   (GetGValue(fill) + 2 * GetGValue(back)) / 3,
                   (GetBValue(fill) + 2 * GetBValue(back)) / 3);
    }
    SetDCBrushColor(dc, fill);
    FillRect(dc, &swatch, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(dc, dark ? RGB(130, 130, 130) : GetSysColor(COLOR_BTNSHADOW));
    FrameRect(dc, &swatch, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if ((item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0) {
        RECT focus = item.rcItem;
        DrawFocusRect(dc, &focus);
    }
}

bool pick_colour(HWND owner, COLORREF& colour) noexcept {
    static COLORREF custom[16] = {};
    CHOOSECOLORW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.rgbResult = colour;
    dialog.lpCustColors = custom;
    dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&dialog)) return false;
    colour = dialog.rgbResult;
    return true;
}

} // namespace filetree::prefs
