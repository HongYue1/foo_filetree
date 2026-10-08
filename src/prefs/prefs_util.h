#pragma once

// Small Win32 helpers for the Preferences page. The page is several child dialogs (one per tab)
// with control ids unique across all of them, so every helper looks a control up by id on the
// page and on each tab.

#include <windows.h>

#include <initializer_list>
#include <string>

namespace filetree::prefs {

//! A control by id on the page itself or on one of its tab dialogs.
[[nodiscard]] HWND find_control(HWND page, int id) noexcept;

[[nodiscard]] std::wstring get_text(HWND page, int id);
void set_text(HWND page, int id, const std::wstring& text);

[[nodiscard]] bool get_check(HWND page, int id) noexcept;
void set_check(HWND page, int id, bool checked) noexcept;

//! An ES_NUMBER field, clamped. Empty or junk reads as `fallback`.
[[nodiscard]] int get_int(HWND page, int id, int low, int high, int fallback) noexcept;
void set_int(HWND page, int id, int value) noexcept;

//! Fills a drop-down list in enum order, so the selection index is the stored value.
void fill_combo(HWND page, int id, std::initializer_list<const wchar_t*> items);
[[nodiscard]] int get_combo(HWND page, int id, int fallback) noexcept;
void set_combo(HWND page, int id, int index) noexcept;

void enable(HWND page, int id, bool enabled) noexcept;

//! "RRGGBB" <-> COLORREF. Junk reads as `fallback`.
[[nodiscard]] COLORREF parse_hex(const std::wstring& text, COLORREF fallback) noexcept;
[[nodiscard]] std::wstring format_hex(COLORREF colour);

//! 4 px (at 96 DPI) inner margins for every Edit on a tab: the default puts text on the border.
void pad_edits(HWND tab) noexcept;

//! Draws an owner-drawn colour swatch button (light and dark mode).
void draw_swatch(const DRAWITEMSTRUCT& item, COLORREF colour) noexcept;

//! ChooseColorW from `colour`. Returns false on Cancel.
[[nodiscard]] bool pick_colour(HWND owner, COLORREF& colour) noexcept;

} // namespace filetree::prefs
