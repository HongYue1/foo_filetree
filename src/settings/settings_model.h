#pragma once

// Component-wide view settings as plain data: defaults, clamping, change detection and the menu
// layout encoding. No fb2k here (settings_store.cpp persists them), so it is unit-tested offline.

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "../fs/enumerate.h"
#include "../model/sort.h"

namespace filetree::settings {

enum class TreeLines : std::uint8_t { none, connectors, guides };
enum class Extensions : std::uint8_t { always, never, non_playable };

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
};
inline constexpr std::size_t menu_item_count = 11;

//! Items that share a group get no separator between them.
[[nodiscard]] int menu_group(MenuItem item) noexcept;
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
    std::uint32_t hidden_drives{0}; //!< bit 0 = A: ... bit 25 = Z:
    bool show_address_bar{true};
    bool show_filter_box{true};

    // Display
    TreeLines lines{TreeLines::none};
    int line_thickness{1};  //!< DIP, 1-4
    bool line_custom_colour{false};
    COLORREF line_colour{RGB(128, 128, 128)};
    int line_opacity{35};   //!< percent of the text colour over the background, 10-100
    Extensions extensions{Extensions::always};
    int row_padding{3};     //!< DIP above and below the text, 0-12
    model::SortOptions sort{};

    // Filter
    bool show_hidden{false};
    bool show_system{false};
    fs::FileMode files{fs::FileMode::playable};
    std::wstring always_show;   //!< extension list, "cue; jpg"
    std::wstring never_show;    //!< extension list
    std::wstring hide_patterns; //!< glob list, "@eaDir; *.tmp"

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

