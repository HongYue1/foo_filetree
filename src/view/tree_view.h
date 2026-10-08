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
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "../actions/action.h"
#include "../actions/shell_ops.h"
#include "../fs/enumerate.h"
#include "../fs/enumeration_service.h"
#include "../model/tree.h"
#include "../settings/settings_store.h"
#include "theme.h"

namespace filetree::view {

class TreeView final : private settings::Listener {
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

    //! Handles the messages the view owns, WM_CONTEXTMENU included (the Default UI host keeps
    //! it in layout-edit mode). Returns false for anything else, including keys it does not use,
    //! so the host can try fb2k's keyboard shortcuts.
    bool handle_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept;

    //! Window styles the hosts must create the window with (beyond WS_CHILD etc.).
    //! WS_CLIPCHILDREN keeps paint off the inline rename editor.
    static constexpr DWORD window_styles = WS_VSCROLL | WS_CLIPCHILDREN;
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
        int line_width{1};  //!< tree line thickness in pixels
    };

    // tree_view.cpp
    void populate_roots();
    void on_settings_changed(std::uint32_t changes) noexcept override;
    //! Filter, sort and display options from the settings, for the next listings and paints.
    void refresh_options() noexcept;
    //! Rebuilds the tree (new filter, sort or roots), re-expanding what was open and restoring
    //! the selection by path as the listings come in.
    void relist_all() noexcept;
    //! Expands / selects `node` if a pending restore wants it.
    void try_restore(std::uint32_t node);
    [[nodiscard]] std::wstring upper_path(std::uint32_t node) const;
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
    //! Sends a node to a playlist per `action` (Kind::send).
    void send_node(const actions::Action& action, std::uint32_t node) noexcept;
    //! Drags a node out of the panel (playlists, playlist tabs, Explorer).
    void drag_node(std::uint32_t node) noexcept;

    // tree_view_menu.cpp
    struct MenuSession;
    void on_context_menu(LPARAM lp) noexcept;
    void run_menu_command(UINT id, std::uint32_t node) noexcept;
    bool forward_menu_message(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept;
    void open_in_explorer(std::uint32_t node) noexcept;
    void copy_path(std::uint32_t node) noexcept;
    void delete_node(std::uint32_t node, bool permanent) noexcept;
    //! A folder lists itself again, a file its parent folder; the selection is kept by name.
    void refresh_node(std::uint32_t node) noexcept;
    //! Reloads `folder` and, once its listing arrives, selects the child called `name` (or
    //! `fallback`, or the folder itself). An empty `name` selects the folder.
    void reload_and_select(std::uint32_t folder, std::wstring name, std::wstring fallback) noexcept;
    void apply_pending_select(std::uint32_t folder) noexcept;
    //! Runs `work` on the main thread later if this view and its tree still exist.
    [[nodiscard]] actions::ShellDone guard(
        std::function<void(TreeView&, actions::ShellResult)> work);
    //! Ctrl+Z: reverts the last rename or Recycle Bin delete made in this panel.
    void undo() noexcept;

    // inline_edit.cpp
    void begin_rename(std::uint32_t node) noexcept;
    void end_rename(bool commit) noexcept;
    bool on_edit_colour(HDC dc, HWND control, LRESULT& result) noexcept;
    static LRESULT CALLBACK edit_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id,
                                      DWORD_PTR data) noexcept;

    // tree_view_paint.cpp
    void paint(HDC target, const RECT& dirty) noexcept;
    void paint_row(HDC dc, std::size_t row, const RECT& rect) noexcept;
    void paint_lines(HDC dc, std::uint32_t index, const RECT& rect, bool expandable) noexcept;
    void ensure_buffer(int width, int height) noexcept;
    void release_buffer() noexcept;

    HWND wnd_{};
    model::Tree tree_;
    ViewColours colours_{};
    // Derived once per set_colours().
    COLORREF hover_background_{};
    COLORREF dim_text_{};
    COLORREF expander_colour_{};
    COLORREF line_colour_{};

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

    // Display options from the settings (refresh_options).
    settings::TreeLines lines_{settings::TreeLines::none};
    settings::Extensions extensions_{settings::Extensions::always};
    std::shared_ptr<const model::ExtensionSet> playable_; //!< for Extensions::non_playable

    // Restore after relist_all(): upper-cased paths still to expand, and the path to select.
    std::unordered_set<std::wstring> restore_expand_;
    std::wstring restore_select_;

    // Context menu (tree_view_menu.cpp): set only while TrackPopupMenu runs.
    MenuSession* menu_{};
    struct PendingSelect {
        std::uint32_t folder{model::no_node};
        std::wstring name;
        std::wstring fallback;
    };
    PendingSelect pending_select_;

    // One level of undo for this panel's own rename/delete (Explorer's undo history is private
    // to Explorer). Paths, not nodes: the tree may have been reloaded since.
    struct UndoRecord {
        enum class Kind : std::uint8_t { none, rename, recycle } kind{Kind::none};
        std::uint32_t folder{model::no_node}; //!< to refresh, if still in this generation
        std::wstring folder_path;
        std::wstring name;     //!< rename: current name; recycle: the deleted item's name
        std::wstring old_name; //!< rename: the name to go back to
    };
    UndoRecord undo_;

    // Inline rename (inline_edit.cpp).
    HWND edit_{};
    std::uint32_t edit_node_{model::no_node};
};

} // namespace filetree::view

