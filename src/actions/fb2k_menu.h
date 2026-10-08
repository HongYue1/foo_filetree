#pragma once

// foobar2000's track context menu (the one playlists show) for one file, as a submenu of ours.
// Main thread only. Filled lazily on WM_INITMENUPOPUP, so nothing is built unless it is opened.

#include <windows.h>

#include <memory>
#include <string>

namespace filetree::actions {

class Fb2kMenu {
public:
    static constexpr UINT first_id = 1000;
    static constexpr UINT id_count = 9000;

    Fb2kMenu();
    ~Fb2kMenu();
    Fb2kMenu(const Fb2kMenu&) = delete;
    Fb2kMenu& operator=(const Fb2kMenu&) = delete;

    void prepare(HMENU submenu, std::wstring path);

    //! WM_INITMENUPOPUP for our submenu fills it. Returns true if it was ours.
    bool handle_message(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept;

    [[nodiscard]] static bool owns(UINT id) noexcept {
        return id >= first_id && id < first_id + id_count;
    }
    void invoke(UINT id) noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace filetree::actions
