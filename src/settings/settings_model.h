#pragma once

// Component-wide view settings as plain data: defaults, clamping, change detection and the menu
// layout encoding. No fb2k here (settings_store.cpp persists them), so it is unit-tested offline.

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../fs/enumerate.h"
#include "../model/sort.h"

namespace filetree::settings {

enum class TreeLines : std::uint8_t { none, connectors, guides };
enum class Extensions : std::uint8_t { always, never, non_playable };
enum class FilterBox : std::uint8_t { bar, floating, off };
//! What a panel shows when foobar2000 starts.
//! last_played: opens down to the track played last (kept across sessions) and selects it.
enum class Startup : std::uint8_t { restore, collapsed, folder, last_played };
enum class FavouritesPlace : std::uint8_t { before, after }; //!< relative to the drives
//! Row tooltips: none, the full name when it is cut off (in place), or the full path.
enum class Tooltips : std::uint8_t { off, truncated, path };

//! Favourites are stored as one string: paths joined by '|' (never part of a Windows path).
[[nodiscard]] std::wstring join_paths(const std::vector<std::wstring>& paths);
//! Splits, trims spaces and trailing backslashes (keeping "C:\"), drops empty and repeated
//! (case-insensitive) entries.
[[nodiscard]] std::vector<std::wstring> split_paths(std::wstring_view text);
//! The path as a favourite: trimmed, no trailing backslash except on a drive ("C:\").
[[nodiscard]] std::wstring clean_path(std::wstring_view path);

//! Folders hidden out of the box: Windows, programs, and the per-drive system folders.
[[nodiscard]] std::vector<std::wstring> default_hidden_folders();

//! Context menu entries a user can show, hide and reorder. Append only (stored by number).
enum class MenuItem : std::uint8_t {
    play,
    add_active,
    new_playlist,
    open_explorer,
    copy_path,
    rename,
    remove,
    refresh,
    undo,
    fb2k_menu,
    explorer_menu,
    favourite, //!< Add to / Remove from favourites
    new_folder,
    paste,
    cut,
    copy,
    queue,
    save_playlist,
    open_with,
    properties,
    hide_folder, //!< adds the folder to the hidden folders
};
inline constexpr std::size_t menu_item_count = 21;

//! Items that share a group get no separator between them.
[[nodiscard]] int menu_group(MenuItem item) noexcept;
//! Items that change files on disk, left out in read-only mode. The Explorer submenu counts
//! (it has Delete, Rename, Cut...).
[[nodiscard]] bool changes_files(MenuItem item) noexcept;
[[nodiscard]] const wchar_t* menu_item_label(MenuItem item) noexcept;

struct MenuLayout {
    std::array<MenuItem, menu_item_count> order{};
    std::uint32_t hidden{0}; //!< bit per MenuItem value

    [[nodiscard]] static MenuLayout defaults() noexcept;
    [[nodiscard]] bool visible(MenuItem item) const noexcept {
        return (hidden & (1u << static_cast<unsigned>(item))) == 0;
    }
    //! "0,1,2,-3,...": item numbers in order, '-' marks hidden. Readable in the config dump.
    [[nodiscard]] std::wstring encode() const;
    //! Never fails: unknown or repeated numbers are skipped and missing items appended in their
    //! default position (new items in a newer version appear, visible).
    [[nodiscard]] static MenuLayout decode(std::wstring_view text);

    friend bool operator==(const MenuLayout&, const MenuLayout&) = default;
};

struct Settings {
    // General
    bool show_drives{true};         //!< off: only the favourites (and library folders) are roots
    std::uint32_t hidden_drives{0}; //!< bit 0 = A: ... bit 25 = Z:
    bool show_address_bar{true};
    FilterBox filter_box{FilterBox::floating};
    Startup startup{Startup::restore};
    bool watch_changes{true}; //!< open folders update when their contents change on disk
    bool follow_playing{false}; //!< each new playing track is opened to and selected
    //! Browse and play only: no rename, delete, cut, paste, new folder, undo or Explorer menu,
    //! no drops on the tree, and drags out never move.
    bool read_only{false};
    std::wstring startup_folder; //!< for Startup::folder
    std::vector<std::wstring> favourites; //!< shown as roots, in this order
    bool library_roots{false}; //!< the Media Library folders join the favourites as roots
    FavouritesPlace favourites_place{FavouritesPlace::before};
    bool separate_favourites{true}; //!< a line between the favourites and the drives
    int favourites_gap{12};         //!< DIP of space between the two groups, 0-24

    // Display
    TreeLines lines{TreeLines::connectors};
    int line_thickness{1};  //!< DIP, 1-4
    bool line_custom_colour{false};
    COLORREF line_colour{RGB(128, 128, 128)};
    int line_opacity{35};   //!< percent of the text colour over the background, 10-100
    Extensions extensions{Extensions::always};
    int row_padding{3};     //!< DIP above and below the text, 0-12
    bool show_icons{true};      //!< folder / file / drive glyphs from the system icon font
    bool mark_favourites{true}; //!< a star after folders that are in the favourites
    bool mark_playing{true};    //!< a play mark on the playing file, or its folder if closed
    int icon_size{16};          //!< file / folder icon size in DIP, 12-24
    int mark_size{12};          //!< star / play mark size in DIP, 8-16
    model::SortOptions sort{};

    // View
    Tooltips tooltips{Tooltips::truncated};
    bool hover_highlight{true}; //!< the row under the mouse is tinted
    bool zebra{false};          //!< every other row slightly tinted
    bool show_status_bar{false}; //!< footer: counts and sizes of the selection or folder
    bool status_counters{false}; //!< the status bar's tooltip shows performance counters
    bool transparent{false};     //!< tree and status bar show what is behind the panel

    // Filter
    bool show_hidden{false};
    bool show_system{false};
    fs::FileMode files{fs::FileMode::playable};
    std::wstring always_show;   //!< extension list, "cue; jpg"
    std::wstring never_show;    //!< extension list
    std::wstring hide_patterns; //!< glob list, "@eaDir; *.tmp"

    // Folders
    //! Never listed (roots excepted). May hold variables ("%WINDIR%") and wildcards ("?:\x").
    std::vector<std::wstring> hidden_folders{default_hidden_folders()};
    bool hide_empty{false}; //!< folders without playable files below them are left out

    // Menu
    MenuLayout menu{MenuLayout::defaults()};

    //! Clamps numbers and enums into range (a hand-edited or newer config cannot break paint).
    void sanitize() noexcept;

    friend bool operator==(const Settings&, const Settings&) = default;
};

enum Change : std::uint32_t {
    change_repaint = 1 << 0,   //!< colours, lines, extension display
    change_remeasure = 1 << 1, //!< row height
    change_relist = 1 << 2,    //!< filter or sort: listings must be redone
    change_roots = 1 << 3,     //!< the root list (hidden drives)
    change_layout = 1 << 4,    //!< panel parts shown or hidden (address bar)
};

//! What a view must do to go from `before` to `after`.
[[nodiscard]] std::uint32_t diff(const Settings& before, const Settings& after) noexcept;

} // namespace filetree::settings

