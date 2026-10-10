#include <helpers/foobar2000+atl.h>

#include "now_playing.h"

#include <algorithm>
#include <vector>

namespace filetree::view::now_playing {
namespace {

// The last local file played, kept for the next session.
constexpr GUID guid_last_played = {
    0x411d3168, 0x88ee, 0x48cb, {0xa3, 0x42, 0xf4, 0xaa, 0x34, 0x4c, 0xf7, 0x82}};
cfg_var_modern::cfg_string cfg_last_played(guid_last_played, "");

std::vector<Listener*> g_listeners;
std::wstring g_path;
bool g_known = false; //!< g_path is current (asked the player, or told since)

std::wstring local_path(const metadb_handle_ptr& track) {
    if (!track.is_valid()) return {};
    const char* path = track->get_path();
    constexpr char prefix[] = "file://";
    if (path == nullptr || strncmp(path, prefix, sizeof(prefix) - 1) != 0) return {};
    return std::wstring(pfc::stringcvt::string_wide_from_utf8(path + sizeof(prefix) - 1).get_ptr());
}

void set(std::wstring path) {
    g_known = true;
    if (path == g_path) return;
    g_path = std::move(path);
    // A listener may unsubscribe while notified.
    const std::vector<Listener*> listeners = g_listeners;
    for (Listener* listener : listeners) {
        if (std::find(g_listeners.begin(), g_listeners.end(), listener) != g_listeners.end()) {
            listener->on_now_playing_changed();
        }
    }
}

class callback : public play_callback_static {
public:
    unsigned get_flags() override { return flag_on_playback_new_track | flag_on_playback_stop; }
    void on_playback_new_track(metadb_handle_ptr track) override {
        try {
            set(local_path(track));
            if (!g_path.empty()) cfg_last_played.set(track->get_path() + 7); // after "file://"
        } catch (...) {
        }
    }
    void on_playback_stop(play_control::t_stop_reason reason) override {
        if (reason == play_control::stop_reason_starting_another) return;
        try {
            set({});
        } catch (...) {
        }
    }
    void on_playback_starting(play_control::t_track_command, bool) override {}
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info&) override {}
    void on_playback_dynamic_info_track(const file_info&) override {}
    void on_playback_time(double) override {}
    void on_volume_change(float) override {}
};

FB2K_SERVICE_FACTORY(callback);

} // namespace

void subscribe(Listener* listener) {
    if (std::find(g_listeners.begin(), g_listeners.end(), listener) == g_listeners.end()) {
        g_listeners.push_back(listener);
    }
}

void unsubscribe(Listener* listener) noexcept {
    std::erase(g_listeners, listener);
}

std::wstring last_played() {
    if (!path().empty()) return g_path;
    return pfc::stringcvt::string_wide_from_utf8(cfg_last_played.get().c_str()).get_ptr();
}

const std::wstring& path() noexcept {
    if (!g_known) {
        // A panel created during playback: ask once, later changes come by callback.
        g_known = true;
        try {
            metadb_handle_ptr track;
            if (playback_control::get()->get_now_playing(track)) g_path = local_path(track);
        } catch (...) {
        }
    }
    return g_path;
}

} // namespace filetree::view::now_playing
