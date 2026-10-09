#pragma once

// The folder tree control. Host-agnostic: the Columns UI panel and the Default UI element each own
// a window and forward its messages here; colours and the font come in through set_colours() and
// set_font().
//
// Performance rules (PLAN.md): paint draws only rows that intersect the invalid rectangle into one
// cached DIB; no allocation in paint, scroll or hover; no timers; disk work only through
// fs::enumeration() on workers. Rows are addressed by index into model::Tree::rows().

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "../actions/action.h"
#include "../actions/shell_ops.h"
#include "../fs/enumerate.h"
#include "../fs/enumeration_service.h"
#include "../fs/watcher.h"
#include "../model/tree.h"
#include "../settings/panel_state.h"
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

    // Navigation (tree_view_nav.cpp), for the address bar and history.
    struct Crumb {
        std::wstring name; //!< as shown: "C:" for a drive
        std::uint32_t node;
    };
    //! Called whenever the selected node changes (or the selection is cleared).
    void set_selection_listener(std::function<void()> listener) {
        selection_listener_ = std::move(listener);
    }
    //! The selected node and its ancestors, root first. Empty without a selection.
    void selection_crumbs(std::vector<Crumb>& out) const;
    [[nodiscard]] std::wstring selected_path() const;
    //! Expands down to `path` (listing folders as needed) and selects it. False when no root
    //! holds the path or it is not an absolute path; nothing changes then.
    bool navigate_to(std::wstring_view path, bool expand_target) noexcept;
    //! Per-instance state for the host to store: open folders, selection, first visible row.
    //! Restores still waiting for a listing (an offline drive) are kept, so they survive.
    void capture_state(settings::PanelState& out) const;
    //! Reopens a captured state; folders open as their listings arrive.
    void restore_state(const settings::PanelState& state) noexcept;
    //! Selects a visible node (an address bar crumb). Ignored if it is not visible.
    void select_node(std::uint32_t node) noexcept;
    void select_parent() noexcept;
    //! Name filter (filter box); empty shows everything again.
    void set_filter(std::wstring_view text) noexcept;
    [[nodiscard]] HWND wnd() const noexcept { return wnd_; }
    //! The font the rows are drawn in, at the window's DPI. Owned by the view.
    [[nodiscard]] HFONT font() const noexcept { return font_; }
    [[nodiscard]] int dpi() const noexcept { return metrics_.dpi; }

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
        int icon{};         //!< icon glyph box (square); 0 without an icon font
        int icon_width{};   //!< icon column before the text, gap included; 0 with icons off
        int group_gap{};    //!< space between the favourites and the drives
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
    //! Keeps restore_top_node_ as the first visible row until the restore finishes or the user
    //! scrolls.
    void apply_restore_top() noexcept;
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
    //! Where the icon column starts (the text when icons are off).
    [[nodiscard]] int content_left(std::uint16_t depth) const noexcept;
    //! The row of the first root after the favourites / drives boundary, if both groups exist.
    [[nodiscard]] std::optional<std::size_t> boundary_row() const noexcept;
    //! The space above `row` from the boundary gap (0 unless the gap is on screen above it).
    [[nodiscard]] int gap_above(std::size_t row) const noexcept;
    //! Client y of a visible row's top edge.
    [[nodiscard]] int row_top(std::size_t row) const noexcept;
    //! In the favourites list (a favourite root, or the same folder elsewhere in the tree).
    [[nodiscard]] bool is_favourite(std::uint32_t index) noexcept;
    void paint_icon(HDC dc, const model::Node& node, const RECT& rect, COLORREF colour) noexcept;

    void select_row(std::size_t row) noexcept;
    void toggle(std::uint32_t node) noexcept;
    void expand(std::uint32_t node) noexcept;
    void collapse(std::uint32_t node) noexcept;
    void request_listing(std::uint32_t node);
    void on_listing(std::uint32_t node, std::uint64_t generation, fs::Listing& listing) noexcept;
    void apply_splice(const model::RowSplice& splice) noexcept;
    void apply_full_splice() noexcept;

    void on_size() noexcept;
    void on_vscroll(int code) noexcept;
    void on_wheel(int delta) noexcept;
    bool on_key(WPARAM key) noexcept;
    //! Type-ahead find: typed characters select the next row whose name starts with them.
    bool on_char(wchar_t ch, DWORD time) noexcept;
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

    // tree_view_nav.cpp
    void notify_selection() noexcept;

    // tree_view_menu.cpp
    struct MenuSession;
    void on_context_menu(LPARAM lp) noexcept;
    void run_menu_command(UINT id, std::uint32_t node) noexcept;
    bool forward_menu_message(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept;
    void open_in_explorer(std::uint32_t node) noexcept;
    void copy_path(std::uint32_t node) noexcept;
    //! Adds the folder to the favourites, or removes it (Preferences > Favourites).
    void toggle_favourite(std::uint32_t node) noexcept;
    void delete_node(std::uint32_t node, bool permanent) noexcept;
    // tree_view_refresh.cpp. Like Explorer's F5: every open folder is listed again in the
    // background and, where something changed, its children are merged in place (open folders
    // stay open, selection and scroll stay). Unchanged folders cost one listing, no repaint.
    void refresh_open_folders() noexcept;
    void request_check(std::uint32_t node);
    void on_check(std::uint32_t node, std::uint64_t generation, fs::Listing& listing) noexcept;
    void retry_failed(std::uint32_t node) noexcept;
    void merge_listing(std::uint32_t node, fs::Listing& listing) noexcept;

    // tree_view_watch.cpp
    static constexpr UINT watch_message = WM_APP + 0x31; //!< LPARAM: fs::WatchId
    void schedule_watch_sync() noexcept;
    void sync_watches() noexcept;
    void unwatch_all() noexcept;
    void stop_watching() noexcept; //!< detach: unwatch and stop the timers
    void on_watch_notify(fs::WatchId id) noexcept;
    bool on_watch_timer(UINT_PTR id) noexcept;
    //! Right-click below the last row: Refresh all, Collapse all, Preferences.
    void show_background_menu(POINT point) noexcept;
    void collapse_all() noexcept;
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
    HFONT icon_font_{}; //!< icons and the favourite star; null without an icon font
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
    std::wstring typeahead_;  //!< characters typed within typeahead_reset_ms of each other
    DWORD typeahead_time_{};
    UINT wheel_lines_{3};

    // Listings in flight. `generation_` changes whenever the tree is rebuilt, so a late result
    // for an old tree is dropped; `alive_` lets callbacks detect that the view is gone.
    struct PendingListing {
        std::uint32_t node;
        fs::EnumerationService::Ticket ticket;
        bool check{false}; //!< a refresh re-check of a loaded folder (on_check), not a load
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
    bool show_icons_{false};
    bool mark_favourites_{false};
    bool separate_favourites_{false};
    std::uint32_t boundary_node_{model::no_node}; //!< first root of the second group
    // Change watching (tree_view_watch.cpp): upper-cased path, node as of the last sync.
    struct Watched {
        fs::WatchId id;
        std::uint32_t node;
        std::wstring path;
    };
    std::vector<Watched> watched_;
    std::vector<fs::WatchId> watch_dirty_;
    bool watch_changes_{true};
    bool sync_pending_{false};
    bool watch_timer_pending_{false};
    mutable std::size_t boundary_row_cache_{0};   //!< checked against boundary_node_ on use
    COLORREF icon_colour_{};
    // Favourites as upper-cased full paths, and their last components for a cheap first test.
    std::unordered_set<std::wstring> favourite_paths_;
    std::unordered_set<std::wstring> favourite_leaves_;
    std::wstring favourite_scratch_; //!< reused by is_favourite
    std::shared_ptr<const model::ExtensionSet> playable_; //!< for Extensions::non_playable

    // Restore after relist_all(): upper-cased paths still to expand, and the path to select.
    std::unordered_set<std::wstring> restore_expand_;
    std::wstring restore_select_;
    std::wstring restore_top_;                  //!< restore_state: the row to show at the top
    std::uint32_t restore_top_node_{model::no_node}; //!< found; kept on top while listings land

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

    std::function<void()> selection_listener_;
    std::uint32_t filter_hidden_selection_{model::no_node}; //!< selected, then filtered out

    // Inline rename (inline_edit.cpp).
    HWND edit_{};
    std::uint32_t edit_node_{model::no_node};
};

} // namespace filetree::view


