#pragma once

// Colours and font the hosts hand to the view. Hosts fill the base colours from their UI
// (Columns UI colour client / Default UI callback); derived colours are computed once here, never
// while painting.

#include <windows.h>

#include <algorithm>

namespace filetree::view {

struct ViewColours {
    COLORREF text{RGB(0, 0, 0)};
    COLORREF background{RGB(255, 255, 255)};
    COLORREF selection_text{RGB(255, 255, 255)};
    COLORREF selection_background{RGB(0, 120, 215)};
    COLORREF inactive_selection_text{RGB(0, 0, 0)};
    COLORREF inactive_selection_background{RGB(204, 204, 204)};
    bool dark{false};
};

//! Linear blend in sRGB space: t = 0 gives a, t = 1 gives b. Good enough for UI tints.
[[nodiscard]] inline COLORREF blend(COLORREF a, COLORREF b, double t) noexcept {
    const auto mix = [t](int x, int y) {
        return static_cast<BYTE>(std::clamp(static_cast<int>(x + (y - x) * t + 0.5), 0, 255));
    };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)),
               mix(GetBValue(a), GetBValue(b)));
}

//! Rough perceived luminance, 0..255.
[[nodiscard]] inline int luminance(COLORREF c) noexcept {
    return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000;
}

//! Whichever of `first` / `second` stands out more against `background`.
[[nodiscard]] inline COLORREF more_contrast(COLORREF background, COLORREF first,
                                            COLORREF second) noexcept {
    const int base = luminance(background);
    return std::abs(luminance(first) - base) >= std::abs(luminance(second) - base) ? first
                                                                                   : second;
}

} // namespace filetree::view
