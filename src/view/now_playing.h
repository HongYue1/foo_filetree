#pragma once

// The playing track as a local path, for the tree's marker and "Show now playing". Fed by a
// play_callback_static (no registration cost, no polling). Main thread only.

#include <string>

namespace filetree::view::now_playing {

class Listener {
public:
    virtual void on_now_playing_changed() noexcept = 0;

protected:
    ~Listener() = default;
};

void subscribe(Listener* listener);
void unsubscribe(Listener* listener) noexcept;

//! Full path of the playing file ("C:\Music\a.flac"), or empty: stopped, or not a plain local
//! file (streams, archives).
[[nodiscard]] const std::wstring& path() noexcept;

} // namespace filetree::view::now_playing
