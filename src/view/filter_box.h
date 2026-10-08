#pragma once

// The filter box: a framed EDIT with a cue banner. Lives in the address bar or floats over the
// top right of the tree (settings::FilterBox); the panel moves it between the two by re-parenting.

#include <windows.h>

#include <functional>
#include <string>

#include "theme.h"

namespace filetree::view {

class FilterBox {
public:
    struct Hooks {
        std::function<void(const std::wstring& text)> changed;
        //! Enter, Down or Esc: go back to the tree. `cleared` when Esc emptied the box.
        std::function<void(bool cleared)> done;
        //! The box lost the focus (floating mode hides an empty box then).
        std::function<void()> blurred;
    };

    FilterBox() = default;
    ~FilterBox() { destroy(); }
    FilterBox(const FilterBox&) = delete;
    FilterBox& operator=(const FilterBox&) = delete;

    bool create(HWND parent, Hooks hooks) noexcept;
    void destroy() noexcept;
    [[nodiscard]] HWND wnd() const noexcept { return wnd_; }

    void set_colours(const ViewColours& colours) noexcept;
    //! A font as the host reports it (system DPI).
    void set_font(const LOGFONTW& font) noexcept;
    void refresh_dpi() noexcept;
    //! Frame height for one line of text.
    [[nodiscard]] int height() const noexcept { return height_; }
    //! Accent border (floating: stands out over the rows).
    void set_floating(bool floating) noexcept;

    void focus() noexcept;
    void clear() noexcept;
    [[nodiscard]] bool active() const noexcept {
        return edit_ != nullptr && GetWindowTextLengthW(edit_) > 0;
    }
    [[nodiscard]] bool has_focus() const noexcept { return edit_ != nullptr && GetFocus() == edit_; }

private:
    static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    static LRESULT CALLBACK edit_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id,
                                      DWORD_PTR data) noexcept;
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    void rebuild_font() noexcept;
    void place_edit() noexcept;

    HWND wnd_{};
    HWND edit_{};
    Hooks hooks_;
    ViewColours colours_{};
    COLORREF border_{};
    bool floating_{false};
    LOGFONTW base_font_{};
    bool has_base_font_{false};
    HFONT font_{};
    int dpi_{96};
    int text_height_{16};
    int height_{22};
};

} // namespace filetree::view
