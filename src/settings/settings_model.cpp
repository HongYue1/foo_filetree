#include "settings_model.h"

#include <algorithm>

namespace filetree::settings {
namespace {

template <typename E>
E clamp_enum(E value, E last) noexcept {
    return static_cast<std::uint8_t>(value) > static_cast<std::uint8_t>(last) ? E{} : value;
}

} // namespace

namespace {

// Where each item goes in a fresh layout (newer items are not always last).
constexpr std::array<MenuItem, menu_item_count> default_order = {
    MenuItem::play,          MenuItem::add_active, MenuItem::new_playlist, MenuItem::queue,
    MenuItem::save_playlist, MenuItem::open_explorer, MenuItem::open_with, MenuItem::properties,
    MenuItem::copy_path,     MenuItem::favourite, MenuItem::hide_folder, MenuItem::new_folder,
    MenuItem::cut,        MenuItem::copy,       MenuItem::paste,        MenuItem::rename,
    MenuItem::remove,     MenuItem::refresh,    MenuItem::undo,         MenuItem::fb2k_menu,
    MenuItem::explorer_menu,
};

std::size_t default_rank(MenuItem item) noexcept {
    for (std::size_t i = 0; i < default_order.size(); ++i) {
        if (default_order[i] == item) return i;
    }
    return default_order.size();
}

} // namespace

bool changes_files(MenuItem item) noexcept {
    switch (item) {
    case MenuItem::new_folder:
    case MenuItem::cut:
    case MenuItem::paste:
    case MenuItem::rename:
    case MenuItem::remove:
    case MenuItem::undo:
    case MenuItem::explorer_menu: return true;
    default: return false;
    }
}

int menu_group(MenuItem item) noexcept {
    switch (item) {
    case MenuItem::play:
    case MenuItem::add_active:
    case MenuItem::new_playlist:
    case MenuItem::queue:
    case MenuItem::save_playlist: return 0;
    case MenuItem::open_explorer:
    case MenuItem::open_with:
    case MenuItem::properties:
    case MenuItem::copy_path:
    case MenuItem::favourite:
    case MenuItem::hide_folder: return 1;
    case MenuItem::new_folder:
    case MenuItem::cut:
    case MenuItem::copy:
    case MenuItem::paste:
    case MenuItem::rename:
    case MenuItem::remove:
    case MenuItem::refresh:
    case MenuItem::undo: return 2;
    case MenuItem::fb2k_menu:
    case MenuItem::explorer_menu: return 3;
    }
    return 4;
}

const wchar_t* menu_item_label(MenuItem item) noexcept {
    switch (item) {
    case MenuItem::play: return L"Play";
    case MenuItem::add_active: return L"Add to active playlist";
    case MenuItem::new_playlist: return L"Send to new playlist";
    case MenuItem::open_explorer: return L"Open in Explorer / Show in folder";
    case MenuItem::copy_path: return L"Copy path";
    case MenuItem::rename: return L"Rename";
    case MenuItem::remove: return L"Delete";
    case MenuItem::refresh: return L"Refresh";
    case MenuItem::undo: return L"Undo (when available)";
    case MenuItem::fb2k_menu: return L"foobar2000 submenu (files)";
    case MenuItem::explorer_menu: return L"Explorer submenu";
    case MenuItem::favourite: return L"Add to / Remove from favourites";
    case MenuItem::hide_folder: return L"Hide this folder";
    case MenuItem::new_folder: return L"New folder";
    case MenuItem::cut: return L"Cut";
    case MenuItem::queue: return L"Add to playback queue";
    case MenuItem::save_playlist: return L"Save as playlist... (folders)";
    case MenuItem::open_with: return L"Open with... (files)";
    case MenuItem::properties: return L"Properties";
    case MenuItem::copy: return L"Copy";
    case MenuItem::paste: return L"Paste (when files are on the clipboard)";
    }
    return L"";
}

MenuLayout MenuLayout::defaults() noexcept {
    MenuLayout out;
    out.order = default_order;
    return out;
}

std::wstring MenuLayout::encode() const {
    std::wstring out;
    for (const MenuItem item : order) {
        if (!out.empty()) out.push_back(L',');
        if (!visible(item)) out.push_back(L'-');
        out += std::to_wstring(static_cast<unsigned>(item));
    }
    return out;
}

MenuLayout MenuLayout::decode(std::wstring_view text) {
    MenuLayout out;
    std::size_t count = 0;
    std::uint32_t seen = 0;
    std::size_t pos = 0;
    while (pos < text.size() && count < menu_item_count) {
        bool hidden = false;
        if (text[pos] == L'-') {
            hidden = true;
            ++pos;
        }
        unsigned value = 0;
        std::size_t digits = 0;
        while (pos < text.size() && text[pos] >= L'0' && text[pos] <= L'9' && digits < 4) {
            value = value * 10 + static_cast<unsigned>(text[pos] - L'0');
            ++pos;
            ++digits;
        }
        while (pos < text.size() && text[pos] != L',') ++pos;
        ++pos; // past ','
        if (digits == 0 || value >= menu_item_count || (seen & (1u << value)) != 0) continue;
        seen |= 1u << value;
        out.order[count++] = static_cast<MenuItem>(value);
        if (hidden) out.hidden |= 1u << value;
    }
    // Items the text did not mention: insert each after its default predecessor, else first.
    for (const MenuItem missing : default_order) {
        const auto value = static_cast<unsigned>(missing);
        if ((seen & (1u << value)) != 0) continue;
        std::size_t at = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (default_rank(out.order[i]) < default_rank(missing)) at = i + 1;
        }
        std::move_backward(out.order.begin() + static_cast<std::ptrdiff_t>(at),
                           out.order.begin() + static_cast<std::ptrdiff_t>(count),
                           out.order.begin() + static_cast<std::ptrdiff_t>(count + 1));
        out.order[at] = static_cast<MenuItem>(value);
        ++count;
        seen |= 1u << value;
    }
    return out;
}

void Settings::sanitize() noexcept {
    hidden_drives &= (1u << 26) - 1;
    lines = clamp_enum(lines, TreeLines::guides);
    line_thickness = std::clamp(line_thickness, 1, 4);
    line_opacity = std::clamp(line_opacity, 10, 100);
    line_colour &= 0xffffff;
    extensions = clamp_enum(extensions, Extensions::non_playable);
    row_padding = std::clamp(row_padding, 0, 12);
    icon_size = std::clamp(icon_size, 12, 24);
    mark_size = std::clamp(mark_size, 8, 16);
    favourites_gap = std::clamp(favourites_gap, 0, 24);
    sort.field = clamp_enum(sort.field, model::SortField::type);
    files = clamp_enum(files, fs::FileMode::none);
    filter_box = clamp_enum(filter_box, FilterBox::off);
    startup = clamp_enum(startup, Startup::last_played);
    favourites_place = clamp_enum(favourites_place, FavouritesPlace::after);
    tooltips = clamp_enum(tooltips, Tooltips::path);
    favourites = split_paths(join_paths(favourites));
    favourite_files = split_paths(join_paths(favourite_files));
    hidden_folders = split_paths(join_paths(hidden_folders));
}

std::uint32_t diff(const Settings& a, const Settings& b) noexcept {
    std::uint32_t out = 0;
    if (a.show_drives != b.show_drives || a.hidden_drives != b.hidden_drives ||
        a.favourites != b.favourites ||
        a.favourite_files.empty() != b.favourite_files.empty() ||
        a.library_roots != b.library_roots ||
        a.favourites_place != b.favourites_place) {
        out |= change_roots;
    }
    if (a.favourite_files != b.favourite_files) out |= change_pinned;
    if (a.lines != b.lines || a.line_thickness != b.line_thickness ||
        a.line_custom_colour != b.line_custom_colour || a.line_colour != b.line_colour ||
        a.line_opacity != b.line_opacity || a.extensions != b.extensions) {
        out |= change_repaint;
    }
    if (a.row_padding != b.row_padding || a.show_icons != b.show_icons ||
        a.icon_size != b.icon_size || a.mark_size != b.mark_size ||
        a.favourites_gap != b.favourites_gap) {
        out |= change_remeasure;
    }
    if (a.mark_favourites != b.mark_favourites || a.mark_playing != b.mark_playing ||
        a.follow_playing != b.follow_playing || a.read_only != b.read_only ||
        a.separate_favourites != b.separate_favourites ||
        a.watch_changes != b.watch_changes || a.tooltips != b.tooltips ||
        a.hover_highlight != b.hover_highlight || a.zebra != b.zebra ||
        a.transparent != b.transparent) {
        out |= change_repaint;
    }
    if (a.show_address_bar != b.show_address_bar || a.filter_box != b.filter_box ||
        a.show_status_bar != b.show_status_bar || a.status_counters != b.status_counters ||
        a.transparent != b.transparent) {
        out |= change_layout;
    }
    if (a.sort.field != b.sort.field || a.sort.folders_first != b.sort.folders_first ||
        a.sort.reverse != b.sort.reverse || a.show_hidden != b.show_hidden ||
        a.show_system != b.show_system || a.files != b.files || a.always_show != b.always_show ||
        a.never_show != b.never_show || a.hide_patterns != b.hide_patterns ||
        a.hidden_folders != b.hidden_folders || a.hide_empty != b.hide_empty) {
        out |= change_relist;
    }
    return out;
}

std::vector<std::wstring> default_hidden_folders() {
    return {L"%WINDIR%",           L"%ProgramFiles%",   L"%ProgramFiles(x86)%",
            L"%ProgramData%",      L"?:\\$Recycle.Bin", L"?:\\System Volume Information"};
}

std::wstring clean_path(std::wstring_view path) {
    while (!path.empty() && (path.front() == L' ' || path.front() == L'"')) path.remove_prefix(1);
    while (!path.empty() && (path.back() == L' ' || path.back() == L'"')) path.remove_suffix(1);
    std::wstring out(path);
    std::replace(out.begin(), out.end(), L'/', L'\\');
    while (out.size() > 1 && out.back() == L'\\' && !(out.size() == 3 && out[1] == L':')) {
        out.pop_back();
    }
    if (out.size() == 2 && out[1] == L':') out.push_back(L'\\');
    return out;
}

std::wstring join_paths(const std::vector<std::wstring>& paths) {
    std::wstring out;
    for (const std::wstring& path : paths) {
        if (path.empty() || path.find(L'|') != std::wstring::npos) continue;
        if (!out.empty()) out.push_back(L'|');
        out += path;
    }
    return out;
}

std::vector<std::wstring> split_paths(std::wstring_view text) {
    std::vector<std::wstring> out;
    while (!text.empty()) {
        const std::size_t bar = text.find(L'|');
        std::wstring path = clean_path(text.substr(0, bar));
        text.remove_prefix(bar == std::wstring_view::npos ? text.size() : bar + 1);
        if (path.empty()) continue;
        const bool repeated = std::any_of(out.begin(), out.end(), [&](const std::wstring& seen) {
            return CompareStringOrdinal(seen.c_str(), static_cast<int>(seen.size()), path.c_str(),
                                        static_cast<int>(path.size()), TRUE) == CSTR_EQUAL;
        });
        if (!repeated) out.push_back(std::move(path));
    }
    return out;
}

} // namespace filetree::settings
