#include <helpers/foobar2000+atl.h>

#include "playlist_send.h"

#include <memory>
#include <vector>

#include "../fs/fb2k_glue.h"
#include "../model/sort.h"
#include "../version.h"
#include "action_settings.h"

namespace filetree::actions {
namespace {

struct Delivery {
    Action action;
    pfc::string8 new_playlist_name;
};

std::size_t resolve_playlist(playlist_manager& pm, const Delivery& delivery) {
    const pfc::string8 temp_name = temp_playlist_name();
    switch (delivery.action.target) {
    case Target::active: {
        const std::size_t active = pm.get_active_playlist();
        if (active != SIZE_MAX) return active;
        return pm.find_or_create_playlist(temp_name);
    }
    case Target::new_playlist:
        return pm.create_playlist(delivery.new_playlist_name, SIZE_MAX, SIZE_MAX);
    case Target::temp:
    default:
        return pm.find_or_create_playlist(temp_name);
    }
}

void deliver(const Delivery& delivery, metadb_handle_list_cref items) {
    if (items.get_count() == 0) return;
    auto pm = playlist_manager::get();
    if (delivery.action.target == Target::queue) {
        for (std::size_t i = 0; i < items.get_count(); ++i) pm->queue_add_item(items[i]);
        return;
    }
    const std::size_t playlist = resolve_playlist(*pm, delivery);
    if (playlist == SIZE_MAX) return;

    if (delivery.action.mode == Mode::replace) {
        pm->playlist_undo_backup(playlist);
        pm->playlist_clear(playlist);
    }
    const std::size_t base = pm->playlist_get_item_count(playlist);
    pm->playlist_add_items(playlist, items, pfc::bit_array_false());

    if (delivery.action.play && pm->playlist_get_item_count(playlist) > base) {
        pm->set_active_playlist(playlist);
        pm->playlist_set_focus_item(playlist, base);
        // Honours the user's "default action" (Play, unless they changed it). The direct
        // alternative, track_command_settrack, is marked internal in playback_control.h.
        pm->playlist_execute_default_action(playlist, base);
    } else if (delivery.action.target == Target::new_playlist) {
        pm->set_active_playlist(playlist); // a new playlist is only useful when you see it
    }
}

void process(std::vector<pfc::string8> locations, Delivery delivery, HWND parent) {
    if (locations.empty()) return;
    pfc::list_t<const char*> urls;
    for (const pfc::string8& location : locations) urls.add_item(location.c_str());

    auto notify = process_locations_notify::create(
        [delivery = std::move(delivery)](metadb_handle_list_cref items) {
            try {
                deliver(delivery, items);
            } catch (const std::exception& error) {
                FB2K_console_formatter() << FILETREE_NAME << ": adding to playlist failed: "
                                         << error.what();
            }
        });
    // Default incoming-item filtering: fb2k's own sort and duplicate rules, and its
    // "restrict/exclude" masks. The progress dialog only appears for slow operations.
    playlist_incoming_item_filter_v2::get()->process_locations_async(
        urls, playlist_incoming_item_filter_v2::op_flag_delay_ui, nullptr, nullptr, parent,
        notify);
}

pfc::string8 to_location(std::wstring_view path) {
    const pfc::stringcvt::string_utf8_from_wide utf8(path.data(), path.size());
    pfc::string8 canonical;
    filesystem::g_get_canonical_path(utf8, canonical);
    return canonical;
}

//! Collects each item's locations in request order: direct items at once, non-recursive folders
//! when their listings land (callbacks run on the main thread). Sends once all are in.
struct Gather {
    std::vector<std::vector<pfc::string8>> parts;
    std::size_t remaining{0};
    Delivery delivery;
    HWND parent{};

    void part_done() {
        if (remaining == 0 || --remaining != 0) return;
        std::vector<pfc::string8> locations;
        for (auto& part : parts) {
            for (auto& location : part) locations.push_back(std::move(location));
        }
        process(std::move(locations), std::move(delivery), parent);
    }
};

void list_folder_files(std::shared_ptr<Gather> gather, std::size_t index, std::wstring folder) {
    // Only this folder's own files: list them on a worker (playable only, display order),
    // then hand the file paths to fb2k.
    fs::EnumOptions options;
    options.files = fs::FileMode::playable;
    options.playable = fs::playable_extensions();
    fs::enumeration().request(
        folder, options, [gather, index, folder](fs::Listing& listing) {
            try {
                std::wstring path;
                for (const auto& item : listing.items) {
                    if (model::is_folder(item.attributes)) continue;
                    path.assign(folder);
                    if (!path.empty() && path.back() != L'\\') path.push_back(L'\\');
                    path.append(listing.name(item));
                    gather->parts[index].push_back(to_location(path));
                }
            } catch (const std::exception& error) {
                FB2K_console_formatter() << FILETREE_NAME << ": " << error.what();
            }
            try {
                gather->part_done();
            } catch (const std::exception& error) {
                FB2K_console_formatter() << FILETREE_NAME << ": " << error.what();
            }
        });
}

} // namespace

void send(const SendRequest& request) noexcept {
    try {
        if (request.action.kind != Kind::send || request.items.empty()) return;

        Delivery delivery{request.action, {}};
        if (request.ctrl) delivery.action.target = Target::active;
        delivery.new_playlist_name =
            pfc::stringcvt::string_utf8_from_wide(request.display_name.c_str()).get_ptr();
        if (delivery.new_playlist_name.is_empty()) delivery.new_playlist_name = temp_playlist_name();

        bool recursive = request.action.recursion == Recursion::always ||
                         (request.action.recursion == Recursion::by_default &&
                          recursive_by_default());
        if (request.shift) recursive = !recursive;

        auto gather = std::make_shared<Gather>();
        gather->delivery = std::move(delivery);
        gather->parent = request.parent;
        gather->parts.resize(request.items.size());
        // +1 holds the send back until every listing has been requested.
        gather->remaining = request.items.size() + 1;
        for (std::size_t i = 0; i < request.items.size(); ++i) {
            const SendItem& item = request.items[i];
            if (item.is_folder && !recursive) {
                list_folder_files(gather, i, item.path);
            } else {
                gather->parts[i].push_back(to_location(item.path));
                gather->part_done();
            }
        }
        gather->part_done();
    } catch (const std::exception& error) {
        FB2K_console_formatter() << FILETREE_NAME << ": send failed: " << error.what();
    } catch (...) {
    }
}

void save_as_playlist(const std::vector<std::wstring>& paths, std::wstring file,
                      HWND parent) noexcept {
    try {
        std::vector<pfc::string8> locations;
        for (const std::wstring& path : paths) locations.push_back(to_location(path));
        pfc::list_t<const char*> urls;
        for (const pfc::string8& location : locations) urls.add_item(location.c_str());
        const pfc::string8 target = pfc::stringcvt::string_utf8_from_wide(file.c_str()).get_ptr();
        auto notify = process_locations_notify::create([target](metadb_handle_list_cref items) {
            try {
                playlist_loader::g_save_playlist(target, items, fb2k::noAbort);
                FB2K_console_formatter() << FILETREE_NAME << ": saved " << items.get_count()
                                         << " tracks to " << target;
            } catch (const std::exception& error) {
                FB2K_console_formatter() << FILETREE_NAME << ": saving " << target
                                         << " failed: " << error.what();
            }
        });
        playlist_incoming_item_filter_v2::get()->process_locations_async(
            urls, playlist_incoming_item_filter_v2::op_flag_delay_ui, nullptr, nullptr, parent,
            notify);
    } catch (const std::exception& error) {
        FB2K_console_formatter() << FILETREE_NAME << ": save as playlist failed: " << error.what();
    } catch (...) {
    }
}

} // namespace filetree::actions
