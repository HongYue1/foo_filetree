#include <helpers/foobar2000+atl.h>

#include "action_settings.h"

#include <optional>

namespace filetree::actions {
namespace {

// Fresh GUIDs, generated 2026-10-08 for foo_filetree. Never reuse.
constexpr GUID guid_bindings = {
    0x0698cf10, 0x6a87, 0x47e4, {0xa9, 0xb4, 0x6b, 0xcb, 0x76, 0xd1, 0x6d, 0xe0}};
constexpr GUID guid_temp_playlist = {
    0x53d7e8e9, 0xb7bd, 0x479a, {0xa9, 0x6c, 0xa3, 0x2a, 0xec, 0xbf, 0xef, 0x77}};
constexpr GUID guid_recursive = {
    0xc4f5a51b, 0x7b2b, 0x4c17, {0x90, 0xca, 0x00, 0xc8, 0xef, 0xfc, 0x01, 0x3e}};

cfg_var_modern::cfg_blob cfg_bindings(guid_bindings);
cfg_var_modern::cfg_string cfg_temp_playlist(guid_temp_playlist, "Folder Tree");
cfg_var_modern::cfg_bool cfg_recursive(guid_recursive, true);

std::optional<Bindings> g_bindings;

} // namespace

const Bindings& bindings() {
    if (!g_bindings) {
        Bindings decoded = Bindings::defaults();
        try {
            if (const auto blob = cfg_bindings.get(); blob.is_valid()) {
                decoded = decode(blob->get_ptr(), blob->size());
            }
        } catch (...) {
        }
        g_bindings = decoded;
    }
    return *g_bindings;
}

void set_bindings(const Bindings& value) {
    std::uint8_t blob[encoded_bindings_size];
    const std::size_t size = encode(value, blob);
    cfg_bindings.set(blob, size);
    g_bindings = value;
}

pfc::string8 temp_playlist_name() {
    pfc::string8 name = cfg_temp_playlist.get();
    if (name.is_empty()) name = "Folder Tree";
    return name;
}

void set_temp_playlist_name(const char* name) { cfg_temp_playlist.set(name); }

bool recursive_by_default() { return cfg_recursive.get(); }

void set_recursive_by_default(bool recursive) { cfg_recursive.set(recursive); }

} // namespace filetree::actions
