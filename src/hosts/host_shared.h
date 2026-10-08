#pragma once

// What the two hosts share. Main thread only.

#include <windows.h>

#include <optional>

namespace filetree::host {

//! Our Columns UI font, or empty when Columns UI is not installed. Both hosts use it first, so the
//! tree looks the same in either UI (defined in cui_panel.cpp).
std::optional<LOGFONTW> cui_font() noexcept;

//! Re-reads fonts in every live Default UI element. Default UI never forwards a Columns UI font
//! change, so the CUI font client calls this (defined in dui_element.cpp).
void refresh_dui_elements() noexcept;

} // namespace filetree::host
