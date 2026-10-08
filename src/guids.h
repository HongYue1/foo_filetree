#pragma once

#include <guiddef.h>

// Fresh GUIDs, generated 2026-10-08 for foo_filetree.
//
// Every GUID here is this component's own. Never copy one from another component (foo_sample,
// foo_mediabar, foo_uie_explorer_mod): a collision breaks both components in one installation.
// Add new GUIDs here rather than inline, so the whole identity surface is one file.

namespace filetree::guids {

// Default UI element (ui_element).
inline constexpr GUID dui_element = {
    0x6f112994, 0x75df, 0x4798, {0x98, 0x95, 0x5d, 0x96, 0x66, 0xfd, 0x05, 0xf3}};

// Columns UI panel (uie::window). Columns UI stores layouts by this GUID; never change it.
inline constexpr GUID cui_panel = {
    0x190fa18d, 0x62bc, 0x46da, {0x80, 0xda, 0x2c, 0xa4, 0x1f, 0x5c, 0x61, 0x57}};

// Columns UI colour client (cui::colours::client): our entry on the CUI Colours page, and the
// instance GUID passed to cui::colours::helper.
inline constexpr GUID cui_colour_client = {
    0x03117098, 0x09b8, 0x46a6, {0x94, 0xe6, 0x00, 0xf1, 0x99, 0x3c, 0xbd, 0x27}};

// Columns UI font client (cui::fonts::client): our entry on the CUI Fonts page, and the font id
// passed to cui::fonts::get_log_font*(). The Default UI element uses it too when CUI is installed.
inline constexpr GUID cui_font_client = {
    0x23cebd68, 0x8587, 0x4619, {0xb0, 0x78, 0xc1, 0x50, 0xea, 0x1a, 0x51, 0xe8}};

// Preferences page (Tools > Folder Tree).
inline constexpr GUID preferences_page = {
    0x0d264917, 0x5950, 0x4e63, {0xb3, 0xf9, 0xe7, 0xe1, 0xd5, 0xfe, 0x3e, 0x47}};

} // namespace filetree::guids
