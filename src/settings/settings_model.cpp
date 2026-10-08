#include "settings_model.h"

#include <algorithm>

namespace filetree::settings {
namespace {

template <typename E>
E clamp_enum(E value, E last) noexcept {
    return static_cast<std::uint8_t>(value) > static_cast<std::uint8_t>(last) ? E{} : value;
}

} // namespace

int menu_group(MenuItem item) noexcept {
    switch (item) {
    case MenuItem::play:
    case MenuItem::add_active:
    case MenuItem::new_playlist: return 0;
    case MenuItem::open_explorer:
    case MenuItem::copy_path: return 1;
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
    }
    return L"";
}

MenuLayout MenuLayout::defaults() noexcept {
    MenuLayout out;
    for (std::size_t i = 0; i < menu_item_count; ++i) out.order[i] = static_cast<MenuItem>(i);
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
    for (unsigned value = 0; value < menu_item_count; ++value) {
        if ((seen & (1u << value)) != 0) continue;
        std::size_t at = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (static_cast<unsigned>(out.order[i]) < value) at = i + 1;
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
    sort.field = clamp_enum(sort.field, model::SortField::type);
    files = clamp_enum(files, fs::FileMode::none);
}

std::uint32_t diff(const Settings& a, const Settings& b) noexcept {
    std::uint32_t out = 0;
    if (a.hidden_drives != b.hidden_drives) out |= change_roots;
    if (a.lines != b.lines || a.line_thickness != b.line_thickness ||
        a.line_custom_colour != b.line_custom_colour || a.line_colour != b.line_colour ||
        a.line_opacity != b.line_opacity || a.extensions != b.extensions) {
        out |= change_repaint;
    }
    if (a.row_padding != b.row_padding) out |= change_remeasure;
    if (a.show_address_bar != b.show_address_bar) out |= change_layout;
    if (a.sort.field != b.sort.field || a.sort.folders_first != b.sort.folders_first ||
        a.sort.reverse != b.sort.reverse || a.show_hidden != b.show_hidden ||
        a.show_system != b.show_system || a.files != b.files || a.always_show != b.always_show ||
        a.never_show != b.never_show || a.hide_patterns != b.hide_patterns) {
        out |= change_relist;
    }
    return out;
}

} // namespace filetree::settings

