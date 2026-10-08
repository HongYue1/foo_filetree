#pragma once

// DPI helpers shared by the view's windows. Windows 7 safe: GetDpiForWindow is looked up at run
// time (a static import would stop the DLL from loading there).

#include <windows.h>

namespace filetree::dpi {

//! The window's DPI (Windows 10 1607+), else the screen's.
[[nodiscard]] inline UINT of_window(HWND wnd) noexcept {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto get_dpi_for_window = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (get_dpi_for_window != nullptr && wnd != nullptr) {
        if (const UINT dpi = get_dpi_for_window(wnd); dpi != 0) return dpi;
    }
    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}

//! The DPI host fonts are reported at (system DPI for a per-monitor-aware process).
[[nodiscard]] inline int system() noexcept {
    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    return dpi > 0 ? dpi : 96;
}

[[nodiscard]] inline int scale(int dips, int dpi) noexcept { return MulDiv(dips, dpi, 96); }

} // namespace filetree::dpi
