#pragma once

// One tooltip tool covering the hovered row of a custom-drawn list. The owner moves the tool
// with each new hover row (which also hides a tip shown for the last one), supplies the text on
// TTN_GETDISPINFO (LPSTR_TEXTCALLBACK, so nothing is built until a tip is due) and may place it
// on TTN_SHOW. TTF_TRANSPARENT lets clicks on an in-place tip reach the row under it.

#include <windows.h>

namespace filetree::view {

class RowTooltip {
public:
    RowTooltip() = default;
    RowTooltip(const RowTooltip&) = delete;
    RowTooltip& operator=(const RowTooltip&) = delete;
    ~RowTooltip() { destroy(); }

    //! Creates the tooltip window once; false if that failed.
    bool ensure(HWND owner) noexcept;
    void destroy() noexcept;
    [[nodiscard]] HWND wnd() const noexcept { return tip_; }

    //! The tool now covers `rect` (owner client coordinates); any tip showing is hidden.
    void set_area(const RECT& rect) noexcept;
    //! No tool area: no tip until set_area again.
    void clear() noexcept;
    void set_dark(bool dark) noexcept;
    //! Call again whenever the owner recreates its font (the tip keeps the handle).
    void set_font(HFONT font) noexcept;

private:
    HWND tip_{};
    HWND owner_{};
    int dark_{-1}; //!< -1: not set yet
};

} // namespace filetree::view
