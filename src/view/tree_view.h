#pragma once

// The folder tree control. Host-agnostic: the Columns UI panel and the Default UI element each own
// a window and forward its messages here; colours and the font come in through set_colours() and
// set_font().
//
// Performance rules (AGENTS.md): paint draws only rows that intersect the invalid rectangle into one
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
#include "../model/library_index.h"
#include "../model/status_text.h"
#include "../model/tree.h"
#include "../settings/panel_state.h"
#include "../settings/settings_store.h"
#include "accessible.h"
#include "drop_target.h"
#include "now_playing.h"
#include "row_tooltip.h"
#include "theme.h"

namespace filetree::view {

class TreeView final : private settings::Listener,
                       private DropSink,
                       private AccessSource,
                       private now_playing::Listener {
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
    //! What the status bar shows: the selection, else the focus row.
    [[nodiscard]] model::Summary status_summary() const noexcept;
    //! Rows, nodes, memory, watches and listings in flight (the status bar's tooltip).
    [[nodiscard]] std::wstring counters_text() const;

    // Main-menu commands (main_menu.cpp) go to the panel that had the focus last.
    enum class Command : std::uint8_t { show_playing, refresh, collapse_all, new_folder };
    //! A new Media Library index: rebuilds the roots if the library folders changed.
    void on_library_changed() noexcept;
    //! The last focused panel, else any; null when no panel exists.
    [[nodiscard]] static TreeView* active() noexcept;
    void run_command(Command command) noexcept;
    //! Alt+Enter: the shell's Properties for the selected item.
    void show_selected_properties() noexcept;

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
        int star_top{};     //!< row-relative top of the star mark's cell: ink centred on the
        int play_top{};     //!< text's capital letters (measure_marks)
    };

    // tree_view.cpp
    void populate_roots();
    void on_settings_changed(std::uint32_t changes) noexcept override;
    //! Filter, sort and display options from the settings, for the next listings and paints.
    void refresh_options() noexcept;
    //! Rebuilds the tree (new filter, sort or roots), re-expanding what was open and restoring
    //! the selection by path as the listings come in.
    //! `keep_pending`: also keep what an earlier restore is still waiting for (the library
    //! roots arrive after the panel restored its state).
    void relist_all(bool keep_pending = false) noexcept;
    //! Expands / selects `node` if a pending restore wants it.
    void try_restore(std::uint32_t node);
    //! Keeps restore_top_node_ as the first visible row until the restore finishes or the user
    //! scrolls.
    void apply_restore_top() noexcept;
    [[nodiscard]] std::wstring upper_path(std::uint32_t node) const;
    void remeasure() noexcept;
    void measure_marks(const TEXTMETRICW& text) noexcept;
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
    //! `playing`: the playing file, drawn as the play triangle.
    void paint_icon(HDC dc, const model::Node& node, const RECT& rect, COLORREF colour,
                    bool playing) noexcept;

    // Selection (tree_view_select.cpp). selected_row_ is the focus row; the selection is the
    // node_selected flag in the tree, the range anchor tree_.anchor().
    //! Selects this row alone and moves the focus to it.
    void select_row(std::size_t row) noexcept;
    //! Moves the focus without changing the selection (Ctrl+arrows).
    void focus_row(std::size_t row) noexcept;
    //! Ctrl+click / Ctrl+Space: flips the row's selection and focuses it; it becomes the anchor.
    void toggle_row(std::size_t row) noexcept;
    //! Shift: selects anchor..row (added to the selection with `add`), focus on row.
    void extend_to(std::size_t row, bool add) noexcept;
    //! A navigation key's target: plain selects, Shift extends, Ctrl only moves the focus.
    void move_to(std::size_t row, bool shift, bool ctrl) noexcept;
    void select_all() noexcept;
    //! The selected nodes in row order; the focus node alone when nothing visible is selected.
    void selection_or_focus(std::vector<std::uint32_t>& out) const;
    void toggle(std::uint32_t node) noexcept;
    void expand(std::uint32_t node) noexcept;
    void collapse(std::uint32_t node) noexcept;
    void request_listing(std::uint32_t node);
    //! The listing options for `node` ("Favourite files" looks up its files instead).
    [[nodiscard]] fs::EnumOptions options_for(std::uint32_t node) const;
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
    // Operations on items (tree_view_items.cpp) act on actions_for(node): the selection when
    // `node` is selected or the focus row, else `node` alone.
    void actions_for(std::uint32_t node, std::vector<std::uint32_t>& out) const;
    void paths_of(const std::vector<std::uint32_t>& nodes, std::vector<std::wstring>& out) const;
    //! All in one folder (one root alone counts too).
    [[nodiscard]] bool same_parent(const std::vector<std::uint32_t>& nodes) const noexcept;
    //! Sends to a playlist per `action` (Kind::send).
    void send_node(const actions::Action& action, std::uint32_t node) noexcept;
    //! Drags out of the panel (playlists, playlist tabs, Explorer).
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
    void hide_folder(std::uint32_t node) noexcept; //!< adds it to the hidden folders
    void delete_node(std::uint32_t node, bool permanent) noexcept;
    // tree_view_refresh.cpp. Like Explorer's F5: every open folder is listed again in the
    // background and, where something changed, its children are merged in place (open folders
    // stay open, selection and scroll stay). Unchanged folders cost one listing, no repaint.
    void refresh_open_folders() noexcept;
    void request_check(std::uint32_t node);
    //! Hide folders with no playable files: after `listing` landed in `node`, a probe listing
    //! searches its child folders and merges the result (on_check). Only when it has folders.
    void request_probe(std::uint32_t node, const fs::Listing& listing);
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
    void reload_and_select(std::uint32_t folder, std::wstring name, std::wstring fallback,
                           bool rename = false) noexcept;
    void apply_pending_select(std::uint32_t folder) noexcept;

    // New folder, clipboard and dropping onto the panel (tree_view_fileops.cpp).
    //! The folder an operation on `node` goes into: itself if a folder, else its parent.
    [[nodiscard]] std::uint32_t target_folder(std::uint32_t node) const noexcept;
    void new_folder(std::uint32_t node) noexcept;
    void put_on_clipboard(std::uint32_t node, bool cut) noexcept;
    //! Shows `nodes` dimmed as cut (empty: none) until the clipboard changes.
    void set_cut(std::vector<std::uint32_t> nodes) noexcept;
    [[nodiscard]] bool is_cut(std::uint32_t node) const noexcept;
    void on_clipboard_update() noexcept;
    void paste_into(std::uint32_t node) noexcept;
    void copy_into(std::uint32_t folder, std::vector<std::wstring> paths, bool move,
                   DWORD clipboard_sequence) noexcept;
    void check_if_open(std::uint32_t folder) noexcept;
    void show_properties(std::uint32_t node) noexcept;
    void save_as_playlist(std::uint32_t node) noexcept;

    // Now playing marker (tree_view_playing.cpp).
    void on_now_playing_changed() noexcept override;
    //! Finds the rows to mark: the playing file if visible, else its deepest visible folder
    //! (under every root that holds it). Cheap; after every row change.
    void resolve_playing() noexcept;
    //! 0: no mark, 1: holds the playing file, 2: is the playing file.
    [[nodiscard]] int playing_mark(std::uint32_t index) const noexcept;
    void set_drop_row(std::ptrdiff_t row) noexcept;
    DWORD drag_over(IDataObject* data, DWORD keys, POINT point, DWORD allowed,
                    bool enter) noexcept override;
    void drag_leave() noexcept override;
    DWORD drop(IDataObject* data, DWORD keys, POINT point, DWORD allowed) noexcept override;
    //! Runs `work` on the main thread later if this view and its tree still exist.
    [[nodiscard]] actions::ShellDone guard(
        std::function<void(TreeView&, actions::ShellResult)> work);
    //! Ctrl+Z: reverts the last rename or Recycle Bin delete made in this panel.
    void undo() noexcept;

    // inline_edit.cpp
    void begin_rename(std::uint32_t node) noexcept;
    void end_rename(bool commit) noexcept;
    //! After rows changed under the editor: move it to its item's row, or cancel if it is gone
    //! or out of view.
    void follow_rename() noexcept;
    [[nodiscard]] RECT rename_rect(std::size_t row) const noexcept;
    bool on_edit_colour(HDC dc, HWND control, LRESULT& result) noexcept;
    static LRESULT CALLBACK edit_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id,
                                      DWORD_PTR data) noexcept;

    // Row tooltips (tree_view_tooltip.cpp).
    //! The hover row changed (-1: none): the tool follows it.
    void update_tooltip(std::ptrdiff_t row) noexcept;
    bool on_tooltip_notify(const NMHDR& header, LRESULT& result) noexcept;
    //! The name as drawn: the extension dropped per the Extensions setting.
    [[nodiscard]] std::wstring_view shown_name(const model::Node& node) const noexcept;
    //! Whether the row cuts its name off (marks after it allowed for); `extent` is its size.
    bool text_cut_off(std::size_t row, SIZE& extent) noexcept;

    // tree_view_access.cpp
    bool on_get_object(WPARAM wp, LPARAM lp, LRESULT& result) noexcept;
    void release_accessible() noexcept;
    void acc_event(DWORD event, std::ptrdiff_t row) const noexcept;
    //! After the focus row moved (or the window got the focus): EVENT_OBJECT_FOCUS.
    void acc_focus_changed() const noexcept;
    [[nodiscard]] std::size_t acc_row_count() const noexcept override;
    bool acc_row(std::size_t row, AccessRow& out) const override;
    bool acc_row_rect(std::size_t row, RECT& out) const noexcept override;
    [[nodiscard]] std::ptrdiff_t acc_focus_row() const noexcept override;
    [[nodiscard]] bool acc_has_focus() const noexcept override;
    [[nodiscard]] std::ptrdiff_t acc_row_at(POINT client) const noexcept override;
    void acc_selected_rows(std::vector<std::size_t>& out) const override;
    void acc_select(std::size_t row, long flags) noexcept override;
    void acc_default_action(std::size_t row) noexcept override;

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
    COLORREF zebra_background_{};
    COLORREF dim_text_{};
    COLORREF expander_colour_{};
    COLORREF line_colour_{};

    LOGFONTW base_font_{};
    bool has_base_font_{false};
    HFONT font_{};
    HFONT icon_font_{}; //!< icons and the favourite star; null without an icon font
    HFONT mark_font_{}; //!< the smaller marks after a name (star, play); with icon_font_
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
    //! The last selection change came from the keyboard: show the focus frame (as Windows hides
    //! focus rectangles until the keyboard is used).
    bool keyboard_cue_{false};
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
        //! Another change was reported while this was in flight: its result may predate it, so
        //! check once more when it lands.
        bool again{false};
    };
    std::vector<PendingListing> pending_;
    std::uint64_t generation_{1};
    std::shared_ptr<TreeView*> alive_;
    std::vector<model::ChildRecord> records_; //!< reused by on_listing
    std::wstring path_;                       //!< reused by request_listing
    fs::EnumOptions options_{};
    //! Settings' favourite files, shared with the workers that look them up.
    std::shared_ptr<const std::vector<std::wstring>> pinned_;
    static constexpr std::wstring_view favourite_files_name = L"Favourite files";

    // Display options from the settings (refresh_options).
    settings::TreeLines lines_{settings::TreeLines::none};
    settings::Extensions extensions_{settings::Extensions::always};
    bool show_icons_{false};
    bool mark_favourites_{false};
    bool separate_favourites_{false};
    bool hover_highlight_{true};
    bool zebra_{false};
    //! Rows in the plain background colour are left unpainted over the parent's background.
    bool transparent_{false};
    settings::Tooltips tooltips_{settings::Tooltips::off};
    RowTooltip tooltip_;
    std::ptrdiff_t tip_row_{-1};
    std::wstring tip_text_; //!< what TTN_GETDISPINFO points at
    bool tip_in_place_{false};
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
    std::shared_ptr<const model::LibraryIndex> library_;  //!< the roots were built from this

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
        bool rename{false}; //!< start renaming the named item (New folder)
    };
    PendingSelect pending_select_;

    // Now playing marker.
    bool mark_playing_{true};
    bool follow_playing_{false}; //!< select each new playing track (setting)
    bool read_only_{false};      //!< no file changes from the panel (setting)
    //! True (and a beep) when read-only mode stops a file change.
    bool refuse_change() const noexcept;
    std::array<std::uint32_t, 4> playing_nodes_{};
    std::size_t playing_count_{0};
    bool playing_exact_{false}; //!< playing_nodes_ are the file itself (not a folder holding it)

    // Cut items (sorted), dimmed while they are what the clipboard holds.
    std::vector<std::uint32_t> cut_nodes_;
    DWORD cut_sequence_{};
    bool clipboard_listening_{false};

    // Dropping onto the panel.
    DropTarget* drop_target_{};
    std::ptrdiff_t drop_row_{-1}; //!< the folder row a drop would go into, highlighted
    bool drop_files_{false};      //!< the current drag carries files
    std::uint32_t drop_hover_node_{model::no_node}; //!< closed folder under the cursor...
    ULONGLONG drop_hover_since_{};                   //!< ...since, to open it after a pause
    ULONGLONG drop_scrolled_at_{};                   //!< auto-scroll pace at the edges

    // One level of undo for this panel's own rename/delete (Explorer's undo history is private
    // to Explorer). Paths, not nodes: the tree may have been reloaded since.
    struct UndoRecord {
        enum class Kind : std::uint8_t { none, rename, recycle } kind{Kind::none};
        std::uint32_t folder{model::no_node}; //!< to refresh, if still in this generation
        std::wstring folder_path;
        std::wstring name;     //!< rename: current name; recycle: the deleted item's name
        std::wstring old_name; //!< rename: the name to go back to
        //! recycle: every deleted item's full path (one delete of several items is one undo)
        std::vector<std::wstring> paths;
    };
    UndoRecord undo_;

    std::function<void()> selection_listener_;
    std::uint32_t filter_hidden_selection_{model::no_node}; //!< selected, then filtered out

    TreeAccessible* accessible_{}; //!< made on the first WM_GETOBJECT

    // Inline rename (inline_edit.cpp).
    HWND edit_{};
    std::uint32_t edit_node_{model::no_node};
};

} // namespace filetree::view


