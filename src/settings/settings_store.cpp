#include <helpers/foobar2000+atl.h>

#include "settings_store.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace filetree::settings {
namespace {

// Fresh GUIDs, generated for foo_filetree M5. Never reuse.
constexpr GUID guid_hidden_drives = {0x81c79c61, 0xc7f3, 0x4ff1, {0x95, 0x11, 0x1f, 0x86, 0x36, 0xa6, 0x68, 0xe3}};
constexpr GUID guid_lines = {0xe6038caf, 0xd7ad, 0x4230, {0x88, 0x8e, 0x78, 0xe6, 0x63, 0x6b, 0x49, 0x8e}};
constexpr GUID guid_line_thickness = {0x5338a530, 0x0086, 0x4723, {0x8b, 0xac, 0x09, 0x40, 0xe1, 0x6c, 0x63, 0xd2}};
constexpr GUID guid_line_custom = {0xa182dfea, 0x5737, 0x4b8d, {0xaa, 0x36, 0x4c, 0x7b, 0x69, 0x54, 0xec, 0xe2}};
constexpr GUID guid_line_colour = {0x27d93641, 0x9833, 0x4832, {0x85, 0x99, 0xb0, 0x1d, 0x2b, 0xed, 0x4a, 0xce}};
constexpr GUID guid_line_opacity = {0x67983199, 0x9897, 0x462e, {0xb9, 0x52, 0xe3, 0x04, 0x3a, 0x28, 0x6c, 0xec}};
constexpr GUID guid_extensions = {0xabb77538, 0x675c, 0x41aa, {0xa9, 0xa7, 0x92, 0x51, 0x61, 0xec, 0x3f, 0xfa}};
constexpr GUID guid_row_padding = {0xb20e2006, 0xcedb, 0x4c4e, {0xb1, 0x17, 0x67, 0x7b, 0x90, 0x62, 0x04, 0xc6}};
constexpr GUID guid_icon_size = {0x5fed82bf, 0x4131, 0x4332, {0x98, 0x64, 0xb6, 0x62, 0xff, 0xb2, 0x68, 0xf7}};
constexpr GUID guid_mark_size = {0xa58a55b4, 0x3737, 0x4d2b, {0xb6, 0x7c, 0xe2, 0x69, 0x62, 0x88, 0xac, 0xb1}};
constexpr GUID guid_sort_field = {0xd7a147e7, 0xdcc5, 0x4b0a, {0x80, 0xe8, 0x33, 0xd7, 0xa7, 0x1c, 0x62, 0xce}};
constexpr GUID guid_folders_first = {0x953a2cf0, 0x0248, 0x4ed2, {0x95, 0xd6, 0xc6, 0x8a, 0xa3, 0xcb, 0xe5, 0x9e}};
constexpr GUID guid_sort_reverse = {0x4e371dd4, 0x7d03, 0x4e94, {0x83, 0xc6, 0x31, 0x07, 0xf9, 0x2b, 0x23, 0x25}};
constexpr GUID guid_show_hidden = {0x075a0fc0, 0x7275, 0x476c, {0xb1, 0x14, 0xa0, 0x1c, 0xf4, 0xb7, 0x91, 0x39}};
constexpr GUID guid_show_system = {0x2370dfc7, 0x2b26, 0x4b55, {0xac, 0x1e, 0x6f, 0x31, 0x44, 0xaa, 0x4c, 0xab}};
constexpr GUID guid_files = {0xad7371b1, 0x9160, 0x4e7e, {0x82, 0xed, 0xbc, 0x93, 0x76, 0x00, 0xf5, 0x19}};
constexpr GUID guid_always_show = {0xfeb7d3e3, 0xe0dd, 0x4434, {0xbd, 0x6f, 0x92, 0xe8, 0x21, 0xeb, 0xd3, 0x78}};
constexpr GUID guid_never_show = {0x50c7f182, 0x6ac2, 0x49e9, {0xba, 0x2d, 0x6f, 0x68, 0xaf, 0x30, 0x48, 0xd3}};
constexpr GUID guid_hide_patterns = {0x262e73df, 0x1412, 0x4717, {0x96, 0x7e, 0xb6, 0x56, 0x4b, 0xd3, 0x0c, 0x87}};
constexpr GUID guid_show_address_bar = {0x961ca913, 0xd41f, 0x43e8, {0x8b, 0x68, 0x5b, 0xd9, 0x1e, 0xbf, 0xff, 0x75}};
constexpr GUID guid_startup = {0x038cf33e, 0xedf7, 0x4107, {0x9a, 0x4d, 0xd5, 0xb8, 0x93, 0xaf, 0x06, 0x17}};
constexpr GUID guid_startup_folder = {0xb670a6f0, 0xb28a, 0x4770, {0xa4, 0x1b, 0x7e, 0xb5, 0x1f, 0xaf, 0xad, 0x2c}};
constexpr GUID guid_favourites = {0x25bdcd87, 0x51a1, 0x4a89, {0xb5, 0x48, 0x5d, 0x8f, 0xa1, 0x6d, 0x63, 0x95}};
constexpr GUID guid_favourites_place = {0xa9cb41bf, 0x0efa, 0x401c, {0xa1, 0x85, 0xab, 0xd9, 0x9c, 0x3a, 0xf3, 0xde}};
constexpr GUID guid_show_icons = {0xf9d0a057, 0x6a52, 0x4cc9, {0xaa, 0x4f, 0x8b, 0x40, 0x85, 0x9e, 0x2c, 0x2b}};
constexpr GUID guid_mark_favourites = {0xe3e51223, 0x74ff, 0x4e48, {0xb7, 0x7d, 0x03, 0x47, 0x9f, 0x5b, 0xe5, 0xd7}};
constexpr GUID guid_mark_playing = {0x27744fb4, 0x9bd4, 0x4490, {0xbc, 0x37, 0x8d, 0x2b, 0xfb, 0x0c, 0x89, 0xa8}};
constexpr GUID guid_follow_playing = {0xa721ea81, 0x7565, 0x4e6a, {0x83, 0x4f, 0xd2, 0x6c, 0x76, 0x93, 0x0b, 0x5d}};
constexpr GUID guid_separate_favourites = {0x9615dedb, 0x4915, 0x47e6, {0x96, 0x7c, 0x15, 0x4b, 0x69, 0xa9, 0x76, 0x8f}};
constexpr GUID guid_favourites_gap = {0xa28695a1, 0xdcf4, 0x413f, {0xae, 0x80, 0xa0, 0x06, 0x01, 0xe2, 0x19, 0x5d}};
constexpr GUID guid_watch_changes = {0x5051725b, 0xdb03, 0x4e50, {0x91, 0x3c, 0x96, 0xf7, 0xad, 0x84, 0x6d, 0x14}};
constexpr GUID guid_filter_box = {0x95efa8c6, 0x13ae, 0x4993, {0xbf, 0xaf, 0x74, 0x7a, 0xeb, 0xa7, 0x74, 0xe3}};
constexpr GUID guid_menu = {0x96e18af9, 0xd800, 0x42bc, {0xad, 0xfd, 0x6b, 0x82, 0xc8, 0x2b, 0x41, 0x7d}};

using cfg_int = cfg_var_modern::cfg_int;
using cfg_bool = cfg_var_modern::cfg_bool;
using cfg_string = cfg_var_modern::cfg_string;

const Settings defaults{};

cfg_int cfg_hidden_drives(guid_hidden_drives, 0);
cfg_int cfg_lines(guid_lines, static_cast<int>(defaults.lines));
cfg_int cfg_line_thickness(guid_line_thickness, defaults.line_thickness);
cfg_bool cfg_line_custom(guid_line_custom, defaults.line_custom_colour);
cfg_int cfg_line_colour(guid_line_colour, static_cast<int>(defaults.line_colour));
cfg_int cfg_line_opacity(guid_line_opacity, defaults.line_opacity);
cfg_int cfg_extensions(guid_extensions, static_cast<int>(defaults.extensions));
cfg_int cfg_row_padding(guid_row_padding, defaults.row_padding);
cfg_int cfg_icon_size(guid_icon_size, defaults.icon_size);
cfg_int cfg_mark_size(guid_mark_size, defaults.mark_size);
cfg_int cfg_sort_field(guid_sort_field, static_cast<int>(defaults.sort.field));
cfg_bool cfg_folders_first(guid_folders_first, defaults.sort.folders_first);
cfg_bool cfg_sort_reverse(guid_sort_reverse, defaults.sort.reverse);
cfg_bool cfg_show_hidden(guid_show_hidden, defaults.show_hidden);
cfg_bool cfg_show_system(guid_show_system, defaults.show_system);
cfg_int cfg_files(guid_files, static_cast<int>(defaults.files));
cfg_string cfg_always_show(guid_always_show, "");
cfg_string cfg_never_show(guid_never_show, "");
cfg_string cfg_hide_patterns(guid_hide_patterns, "");
cfg_string cfg_menu(guid_menu, "");
cfg_bool cfg_show_address_bar(guid_show_address_bar, defaults.show_address_bar);
cfg_bool cfg_separate_favourites(guid_separate_favourites, defaults.separate_favourites);
cfg_int cfg_favourites_gap(guid_favourites_gap, defaults.favourites_gap);
cfg_bool cfg_watch_changes(guid_watch_changes, defaults.watch_changes);
cfg_bool cfg_show_icons(guid_show_icons, defaults.show_icons);
cfg_bool cfg_mark_favourites(guid_mark_favourites, defaults.mark_favourites);
cfg_bool cfg_mark_playing(guid_mark_playing, defaults.mark_playing);
cfg_bool cfg_follow_playing(guid_follow_playing, defaults.follow_playing);
cfg_int cfg_filter_box(guid_filter_box, static_cast<int>(defaults.filter_box));
cfg_int cfg_startup(guid_startup, static_cast<int>(defaults.startup));
cfg_string cfg_startup_folder(guid_startup_folder, "");
cfg_string cfg_favourites(guid_favourites, "");
cfg_int cfg_favourites_place(guid_favourites_place, static_cast<int>(defaults.favourites_place));

std::optional<Settings> g_current; //!< saved
std::optional<Settings> g_preview;
std::shared_ptr<const model::FilterRules> g_rules;
std::vector<Listener*> g_listeners;

std::wstring wide(const pfc::string8& text) {
    return pfc::stringcvt::string_wide_from_utf8(text.c_str()).get_ptr();
}

pfc::string8 utf8(const std::wstring& text) {
    return pfc::stringcvt::string_utf8_from_wide(text.c_str()).get_ptr();
}

template <typename E>
E as_enum(std::int64_t value) {
    return static_cast<E>(static_cast<std::uint8_t>(std::clamp<std::int64_t>(value, 0, 255)));
}

int as_int(std::int64_t value) {
    return static_cast<int>(std::clamp<std::int64_t>(value, INT32_MIN, INT32_MAX));
}

Settings load() {
    Settings s;
    s.hidden_drives = static_cast<std::uint32_t>(cfg_hidden_drives.get());
    s.lines = as_enum<TreeLines>(cfg_lines.get());
    s.line_thickness = as_int(cfg_line_thickness.get());
    s.line_custom_colour = cfg_line_custom.get();
    s.line_colour = static_cast<COLORREF>(cfg_line_colour.get());
    s.line_opacity = as_int(cfg_line_opacity.get());
    s.extensions = as_enum<Extensions>(cfg_extensions.get());
    s.row_padding = as_int(cfg_row_padding.get());
    s.icon_size = as_int(cfg_icon_size.get());
    s.mark_size = as_int(cfg_mark_size.get());
    s.sort.field = as_enum<model::SortField>(cfg_sort_field.get());
    s.sort.folders_first = cfg_folders_first.get();
    s.sort.reverse = cfg_sort_reverse.get();
    s.show_hidden = cfg_show_hidden.get();
    s.show_system = cfg_show_system.get();
    s.files = as_enum<fs::FileMode>(cfg_files.get());
    s.always_show = wide(cfg_always_show.get());
    s.never_show = wide(cfg_never_show.get());
    s.hide_patterns = wide(cfg_hide_patterns.get());
    s.menu = MenuLayout::decode(wide(cfg_menu.get()));
    s.show_address_bar = cfg_show_address_bar.get();
    s.show_icons = cfg_show_icons.get();
    s.watch_changes = cfg_watch_changes.get();
    s.separate_favourites = cfg_separate_favourites.get();
    s.favourites_gap = static_cast<int>(cfg_favourites_gap.get());
    s.mark_favourites = cfg_mark_favourites.get();
    s.mark_playing = cfg_mark_playing.get();
    s.follow_playing = cfg_follow_playing.get();
    s.filter_box = as_enum<FilterBox>(cfg_filter_box.get());
    s.startup = as_enum<Startup>(cfg_startup.get());
    s.startup_folder = wide(cfg_startup_folder.get());
    s.favourites = split_paths(wide(cfg_favourites.get()));
    s.favourites_place = as_enum<FavouritesPlace>(cfg_favourites_place.get());
    s.sanitize();
    return s;
}

void save(const Settings& s) {
    cfg_hidden_drives.set(s.hidden_drives);
    cfg_lines.set(static_cast<int>(s.lines));
    cfg_line_thickness.set(s.line_thickness);
    cfg_line_custom.set(s.line_custom_colour);
    cfg_line_colour.set(static_cast<int>(s.line_colour));
    cfg_line_opacity.set(s.line_opacity);
    cfg_extensions.set(static_cast<int>(s.extensions));
    cfg_row_padding.set(s.row_padding);
    cfg_icon_size.set(s.icon_size);
    cfg_mark_size.set(s.mark_size);
    cfg_sort_field.set(static_cast<int>(s.sort.field));
    cfg_folders_first.set(s.sort.folders_first);
    cfg_sort_reverse.set(s.sort.reverse);
    cfg_show_hidden.set(s.show_hidden);
    cfg_show_system.set(s.show_system);
    cfg_files.set(static_cast<int>(s.files));
    cfg_always_show.set(utf8(s.always_show));
    cfg_never_show.set(utf8(s.never_show));
    cfg_hide_patterns.set(utf8(s.hide_patterns));
    cfg_menu.set(utf8(s.menu.encode()));
    cfg_show_address_bar.set(s.show_address_bar);
    cfg_show_icons.set(s.show_icons);
    cfg_watch_changes.set(s.watch_changes);
    cfg_separate_favourites.set(s.separate_favourites);
    cfg_favourites_gap.set(s.favourites_gap);
    cfg_mark_favourites.set(s.mark_favourites);
    cfg_mark_playing.set(s.mark_playing);
    cfg_follow_playing.set(s.follow_playing);
    cfg_filter_box.set(static_cast<int>(s.filter_box));
    cfg_startup.set(static_cast<int>(s.startup));
    cfg_startup_folder.set(utf8(s.startup_folder));
    cfg_favourites.set(utf8(join_paths(s.favourites)));
    cfg_favourites_place.set(static_cast<int>(s.favourites_place));
}

std::shared_ptr<const model::FilterRules> build_rules(const Settings& s) {
    auto rules = std::make_shared<model::FilterRules>();
    rules->always_show.add_mask(s.always_show);
    rules->never_show.add_mask(s.never_show);
    rules->set_hide_patterns(s.hide_patterns);
    if (rules->empty()) return nullptr;
    return rules;
}

} // namespace

const Settings& stored() {
    if (!g_current) {
        g_current = load();
        if (!g_preview) g_rules = build_rules(*g_current);
    }
    return *g_current;
}

const Settings& current() {
    if (g_preview) return *g_preview;
    return stored();
}

namespace {

//! After the effective settings changed from `before`: rebuild the rules, tell the panels.
void publish(const Settings& before) {
    const std::uint32_t changes = diff(before, current());
    if ((changes & change_relist) != 0) g_rules = build_rules(current());
    if (changes == 0) return; // e.g. only the menu layout: read when the menu opens
    // Copy: a listener may unsubscribe while being notified.
    const std::vector<Listener*> listeners = g_listeners;
    for (Listener* listener : listeners) {
        if (std::find(g_listeners.begin(), g_listeners.end(), listener) != g_listeners.end()) {
            listener->on_settings_changed(changes);
        }
    }
}

} // namespace

void apply(Settings next) {
    next.sanitize();
    const Settings before = current();
    if (!(stored() == next)) save(next);
    g_current = std::move(next);
    g_preview.reset();
    publish(before);
}

void preview(Settings next) {
    next.sanitize();
    const Settings before = current();
    g_preview = std::move(next);
    publish(before);
}

void end_preview() {
    if (!g_preview) return;
    const Settings before = *g_preview;
    g_preview.reset();
    publish(before);
}

std::shared_ptr<const model::FilterRules> filter_rules() {
    (void)current();
    return g_rules;
}

void subscribe(Listener* listener) { g_listeners.push_back(listener); }

void unsubscribe(Listener* listener) noexcept {
    std::erase(g_listeners, listener);
}

} // namespace filetree::settings

