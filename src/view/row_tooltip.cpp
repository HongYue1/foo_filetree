#include "row_tooltip.h"

#include <commctrl.h>
#include <uxtheme.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace filetree::view {
namespace {

constexpr UINT_PTR tool_id = 1;

TTTOOLINFOW tool_info(HWND owner) noexcept {
    TTTOOLINFOW info{};
    info.cbSize = sizeof(info);
    info.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
    info.hwnd = owner;
    info.uId = tool_id;
    info.lpszText = LPSTR_TEXTCALLBACKW;
    return info;
}

} // namespace

bool RowTooltip::ensure(HWND owner) noexcept {
    if (tip_ != nullptr) return true;
    if (owner == nullptr) return false;
    tip_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT, TOOLTIPS_CLASSW, nullptr,
                           WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                           CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr, nullptr, nullptr);
    if (tip_ == nullptr) return false;
    owner_ = owner;
    TTTOOLINFOW info = tool_info(owner);
    if (SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info)) == FALSE) {
        destroy();
        return false;
    }
    // Long paths wrap at about two thirds of the screen rather than running off it.
    SendMessageW(tip_, TTM_SETMAXTIPWIDTH, 0, GetSystemMetrics(SM_CXSCREEN) * 2 / 3);
    return true;
}

void RowTooltip::destroy() noexcept {
    if (tip_ != nullptr) DestroyWindow(tip_);
    tip_ = nullptr;
    owner_ = nullptr;
    dark_ = -1;
}

void RowTooltip::set_area(const RECT& rect) noexcept {
    if (tip_ == nullptr) return;
    SendMessageW(tip_, TTM_POP, 0, 0);
    TTTOOLINFOW info = tool_info(owner_);
    info.rect = rect;
    SendMessageW(tip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&info));
}

void RowTooltip::clear() noexcept { set_area(RECT{}); }

void RowTooltip::set_dark(bool dark) noexcept {
    if (tip_ == nullptr || dark_ == static_cast<int>(dark)) return;
    dark_ = static_cast<int>(dark);
    SetWindowTheme(tip_, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

void RowTooltip::set_font(HFONT font) noexcept {
    if (tip_ == nullptr || font == nullptr) return;
    SendMessageW(tip_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
}

} // namespace filetree::view
