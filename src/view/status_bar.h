#pragma once

// The optional status bar under the tree (M8b): one line of text (model::format_summary) in a
// dimmed text colour over the panel background, a hairline on top. Owns its font, scaled to its
// window's DPI like the filter box. Resting the mouse on it shows the panel's counters.

#include <windows.h>

#include <functional>
#include <string>

#include "row_tooltip.h"
#include "theme.h"

namespace filetree::view {

class StatusBar {
public:
    StatusBar() = default;
    ~StatusBar() { destroy(); }
    StatusBar(const StatusBar&) = delete;
    StatusBar& operator=(const StatusBar&) = delete;

    //! `counters` is asked for the tooltip text when a tip is due.
    bool create(HWND parent, std::function<std::wstring()> counters) noexcept;
    void destroy() noexcept;
    [[nodiscard]] HWND wnd() const noexcept { return wnd_; }

    void set_colours(const ViewColours& colours) noexcept;
    //! A font as the host reports it (system DPI).
    void set_font(const LOGFONTW& font) noexcept;
    void refresh_dpi() noexcept;
    [[nodiscard]] int height() const noexcept { return height_; }

    //! The performance-counter tooltip (off: no tip at all).
    void set_counters_enabled(bool enabled) noexcept;

    //! Repaints only when the text changed.
    void set_text(std::wstring text) noexcept;

private:
    static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    void rebuild_font() noexcept;
    void paint(HDC dc) noexcept;

    HWND wnd_{};
    std::function<std::wstring()> counters_;
    std::wstring text_;
    std::wstring tip_text_; //!< what TTN_GETDISPINFO points at
    RowTooltip tooltip_;
    ViewColours colours_{};
    COLORREF text_colour_{};
    COLORREF line_colour_{};
    LOGFONTW base_font_{};
    bool has_base_font_{false};
    HFONT font_{};
    int dpi_{96};
    int height_{20};
    bool counters_enabled_{false};
    void place_tip() noexcept;
};

} // namespace filetree::view
