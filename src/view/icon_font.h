#pragma once

// Icons are glyphs of the system icon font (Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets
// on Windows 10): drawn as text, so they take the theme's text colours (light, dark, selection)
// and scale with DPI, with no image list and no disk access.

namespace filetree::view {

//! The icon font's face name, or nullptr when neither font is installed. Looked up once.
[[nodiscard]] const wchar_t* icon_font_face() noexcept;

namespace glyph {
inline constexpr wchar_t folder = 0xE8B7;
inline constexpr wchar_t folder_open = 0xE838;
inline constexpr wchar_t document = 0xE8A5;
inline constexpr wchar_t audio = 0xE8D6;
inline constexpr wchar_t drive = 0xEDA2;
inline constexpr wchar_t star = 0xE735;          //!< filled star (icon font)
inline constexpr wchar_t star_fallback = 0x2605; //!< BLACK STAR, any UI font
} // namespace glyph

} // namespace filetree::view
