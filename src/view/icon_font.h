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
inline constexpr wchar_t video = 0xE714;    //!< Video (camera)
inline constexpr wchar_t image = 0xEB9F;    //!< Photo
inline constexpr wchar_t text = 0xF000;     //!< a page with lines
inline constexpr wchar_t pdf = 0xEA90;      //!< PDF
inline constexpr wchar_t playlist = 0xE90B; //!< MusicInfo: lines with a note
inline constexpr wchar_t drive = 0xEDA2;
inline constexpr wchar_t star = 0xE735;          //!< filled star (icon font)
inline constexpr wchar_t star_fallback = 0x2605; //!< BLACK STAR, any UI font
inline constexpr wchar_t playing = 0xF5B0;          //!< PlaySolid (icon font): now playing
inline constexpr wchar_t playing_fallback = 0x25B6; //!< BLACK RIGHT-POINTING TRIANGLE
} // namespace glyph

} // namespace filetree::view
