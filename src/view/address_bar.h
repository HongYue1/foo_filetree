#pragma once

// The address bar above the tree: Back, Forward and Up buttons, then the selection's path as
// crumbs. Clicking a crumb selects that folder; clicking the empty part (or Ctrl+L in the tree)
// turns the crumbs into an edit box for typing a path. Painted with GDI like the tree; the
// layout is measured when the crumbs, font or size change, never while painting.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "theme.h"
#include "tree_view.h"

namespace filetree::view {

class AddressBar {
public:
    enum Button : int { back, forward, up, button_count };

    struct Hooks {
        std::function<void(std::uint32_t node)> crumb;          //!< a crumb was clicked
        std::function<bool(const std::wstring& path)> navigate; //!< Enter; false keeps editing
        std::function<void(Button button)> button;
        std::function<void()> done; //!< editing ended with Enter or Esc: focus the tree
    };

    AddressBar() = default;
    ~AddressBar() { destroy(); }
    AddressBar(const AddressBar&) = delete;
    AddressBar& operator=(const AddressBar&) = delete;

    bool create(HWND parent, Hooks hooks) noexcept;
    void destroy() noexcept;
    [[nodiscard]] HWND wnd() const noexcept { return wnd_; }

    void set_colours(const ViewColours& colours) noexcept;
    //! Draw what is behind the panel instead of the bar's tint (buttons, crumbs, hover stay).
    void set_transparent(bool transparent) noexcept;
    //! A font as the host reports it (system DPI); scaled to the window's DPI here.
    void set_font(const LOGFONTW& font) noexcept;
    //! Call after a DPI change: rebuilds the font and the layout.
    void refresh_dpi() noexcept;
    [[nodiscard]] int height() const noexcept { return height_; }

    void set_crumbs(std::vector<TreeView::Crumb> crumbs, std::wstring path) noexcept;
    void set_enabled(bool back_enabled, bool forward_enabled, bool up_enabled) noexcept;
    void begin_edit() noexcept;

    //! Which parts show: the address (buttons + crumbs), and room on the right for a filter box
    //! of `filter_height` (0 = none). The panel places the FilterBox in filter_rect().
    void set_parts(bool address, int filter_height) noexcept;
    [[nodiscard]] RECT filter_rect() const noexcept { return filter_rect_; }

private:
    // Hit codes: buttons are 0..2, crumbs crumb_hit + index.
    static constexpr int hit_none = -1;
    static constexpr int hit_overflow = 8;
    static constexpr int hit_blank = 9;
    static constexpr int crumb_hit = 16;

    static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    static LRESULT CALLBACK edit_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id,
                                      DWORD_PTR data) noexcept;

    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    void rebuild_font() noexcept;
    void layout() noexcept;
    void paint(HDC dc, const RECT& client) noexcept;
    [[nodiscard]] int hit(int x, int y) const noexcept;
    void activate(int code) noexcept;
    void end_edit(bool commit) noexcept;
    void set_hover(int code) noexcept;

    HWND wnd_{};
    HWND edit_{};
    bool show_address_{true};
    bool transparent_{false};
    int filter_height_{0};
    RECT filter_rect_{}; //!< where the panel puts the filter box
    Hooks hooks_;
    ViewColours colours_{};
    COLORREF bar_background_{};
    COLORREF hover_background_{};
    COLORREF dim_text_{};
    COLORREF border_{};

    LOGFONTW base_font_{};
    bool has_base_font_{false};
    HFONT font_{};
    int dpi_{96};
    int height_{24};
    int text_height_{16};

    std::vector<TreeView::Crumb> crumbs_;
    std::wstring path_;
    std::vector<RECT> crumb_rects_;   //!< per crumb; empty rect = scrolled off to the left
    RECT overflow_rect_{};            //!< "..." when leading crumbs do not fit
    bool enabled_[button_count]{};
    int hover_{hit_none};
    int pressed_{hit_none};
    bool tracking_{false};
};

} // namespace filetree::view
