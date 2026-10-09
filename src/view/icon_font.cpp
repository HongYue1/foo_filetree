#include "icon_font.h"

#include <windows.h>

#include <cwchar>

namespace filetree::view {
namespace {

bool font_installed(const wchar_t* face) noexcept {
    HDC dc = GetDC(nullptr);
    if (dc == nullptr) return false;
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(query.lfFaceName, face, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(
        dc, &query,
        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM data) -> int {
            *reinterpret_cast<bool*>(data) = true;
            return 0;
        },
        reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

} // namespace

const wchar_t* icon_font_face() noexcept {
    static const wchar_t* const face = []() -> const wchar_t* {
        if (font_installed(L"Segoe Fluent Icons")) return L"Segoe Fluent Icons";
        if (font_installed(L"Segoe MDL2 Assets")) return L"Segoe MDL2 Assets";
        return nullptr;
    }();
    return face;
}

} // namespace filetree::view
