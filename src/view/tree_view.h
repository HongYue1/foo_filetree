#pragma once

// The folder tree control. Host-agnostic: the Columns UI panel and the Default UI element each own
// a window and forward its messages here; colours and the font come in through set_colours() and
// set_font().
//
// Performance rules (PLAN.md): paint draws only rows that intersect the invalid rectangle into one
// cached DIB; no allocation in paint, scroll or hover; no timers; disk work only through
// fs::enumeration() on workers. Rows are addressed by index into model::Tree::rows().

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../actions/action.h"
#include "../fs/enumerate.h"
#include "../fs/enumeration_service.h"
#include "../model/tree.h"
#include "theme.h"

namespace filetree::view {

class TreeView {
public:
    TreeView();
    ~TreeView();

    TreeView(const TreeView&) = delete;
    TreeView& operator=(const TreeView&) = delete;

    //! Call from WM_CREATE. Fills the roots (drives) and measures.
    void attach(HWND wnd) noexcept;
    //! Call from WM_DESTROY. Cancels outstanding listings and frees GDI objects.
    void detach() noexcept;

    void set_colours(const ViewColours& colours) noexcept;
    //! A font as the host reports it, sized for the system DPI; the view scales it to the
    //! window's DPI itself.
    void set_font(const LOGFONTW& font) noexcept;

    //! Handles the messages the view owns. Returns false for anything else (including keys it
    //! does not use, so the host can try fb2k's keyboard shortcuts) and for WM_CONTEXTMENU.
    bool handle_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept;

    //! Window styles the hosts must create the window with (beyond WS_CHILD etc.).
    static constexpr DWORD window_styles = WS_VSCROLL;
    //! Class styles the hosts must register (double-click toggles folders).
    static constexpr UINT class_styles = CS_DBLCLKS;

private:
    // Layout, in pixels at the current DPI. Recomputed on font/DPI change only.
    struct Metrics {
        int dpi{96};
        int row_height{20};
        int indent{16};
        int expander{9};    //!< glyph box size
        int text_gap{4};    //!< between the expander column and the text
        int text_ascent{};
    };

    // tree_view.cpp
    void populate_roots();
    void remeasure() noexcept;
    void rebuild_font() noexcept;
    void update_scrollbar() noexcept;
    [[nodiscard]] int visible_rows() const noexcept;
    [[nodiscard]] std::size_t max_top_row() const noexcept;
    void scroll_to(std::size_t top_row) noexcept;
    void ensure_visible(std::size_t row) noexcept;
    void invalidate_row(std::size_t row) noexcept;
    void invalidate_from(std::size_t row) noexcept;
    [[nodiscard]] std::ptrdiff_t row_at(int y) const noexcept;
    [[nodiscard]] int expander_left(std::uint16_t depth) const noexcept;
    [[nodiscard]] int text_left(std::uint16_t depth) const noexcept;

    void select_row(std::size_t row) noexcept;
    void toggle(std::uint32_t node) noexcept;
    void expand(std::uint32_t node) noexcept;
    void collapse(std::uint32_t node) noexcept;
    void request_listing(std::uint32_t node);
    void on_listing(std::uint32_t node, std::uint64_t generation, fs::Listing& listing) noexcept;
    void apply_splice(const model::RowSplice& splice) noexcept;

    void on_size() noexcept;
    void on_vscroll(int code) noexcept;
    void on_wheel(int delta) noexcept;
    bool on_key(WPARAM key) noexcept;
    void on_button_down(int x, int y, bool double_click) noexcept;
    void on_middle_button(int y) noexcept;
    //! Runs the bound action for a gesture on a row's node (actions/action_settings.h).
    void run_gesture(actions::Gesture gesture, std::uint32_t node) noexcept;
    void on_mouse_move(int x, int y) noexcept;
    void on_mouse_leave() noexcept;
    void on_dpi_changed() noexcept;

    // tree_view_paint.cpp
    void paint(HDC target, const RECT& dirty) noexcept;
    void paint_row(HDC dc, std::size_t row, const RECT& rect) noexcept;
    void ensure_buffer(int width, int height) noexcept;
    void release_buffer() noexcept;

    HWND wnd_{};
    model::Tree tree_;
    ViewColours colours_{};
    // Derived once per set_colours().
    COLORREF hover_background_{};
    COLORREF dim_text_{};
    COLORREF expander_colour_{};

    LOGFONTW base_font_{};
    bool has_base_font_{false};
    HFONT font_{};
    Metrics metrics_{};

    // Back buffer: one DIB, grown on WM_SIZE, reused by every paint.
    HDC buffer_dc_{};
    HBITMAP buffer_bitmap_{};
    HGDIOBJ buffer_old_bitmap_{};
    int buffer_width_{};
    int buffer_height_{};

    int client_width_{};
    int client_height_{};
    std::size_t top_row_{};
    std::ptrdiff_t selected_row_{-1};
    std::ptrdiff_t hover_row_{-1};
    bool focused_{false};
    bool tracking_mouse_{false};
    int wheel_remainder_{};
    UINT wheel_lines_{3};

    // Listings in flight. `generation_` changes whenever the tree is rebuilt, so a late result
    // for an old tree is dropped; `alive_` lets callbacks detect that the view is gone.
    struct PendingListing {
        std::uint32_t node;
        fs::EnumerationService::Ticket ticket;
    };
    std::vector<PendingListing> pending_;
    std::uint64_t generation_{1};
    std::shared_ptr<TreeView*> alive_;
    std::vector<model::ChildRecord> records_; //!< reused by on_listing
    std::wstring path_;                       //!< reused by request_listing
    fs::EnumOptions options_{};
};

} // namespace filetree::view
