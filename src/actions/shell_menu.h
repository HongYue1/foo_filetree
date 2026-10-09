#pragma once

// The Windows Explorer context menu for one path, or several in one folder, as a submenu of ours. Main thread only.
//
// The submenu is filled lazily when the user opens it (WM_INITMENUPOPUP), so the shell extension
// handlers (which may touch the disk or the network) only load if the user asks for them. While
// our menu is tracked, the owner window forwards owner-draw and submenu messages here
// (IContextMenu2/3 need them for "Open with", "Send to", icons).

#include <windows.h>

#include <shobjidl.h>

#include <string>
#include <vector>

namespace filetree::actions {

class ShellMenu {
public:
    static constexpr UINT first_id = 10000;
    static constexpr UINT last_id = 0x7fff;

    ShellMenu() = default;
    ~ShellMenu() { reset(); }
    ShellMenu(const ShellMenu&) = delete;
    ShellMenu& operator=(const ShellMenu&) = delete;

    //! Remembers what to fill `submenu` with. Several paths must share one parent folder.
    //! `extended` adds Shift-only verbs.
    void prepare(HMENU submenu, std::vector<std::wstring> paths, HWND owner, bool extended);

    //! WM_INITMENUPOPUP, WM_DRAWITEM, WM_MEASUREITEM, WM_MENUCHAR while our menu is open.
    //! Returns true if the message was for the shell menu.
    bool handle_message(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept;

    [[nodiscard]] static bool owns(UINT id) noexcept { return id >= first_id && id <= last_id; }
    //! Runs a chosen shell command. `point` is in screen coordinates.
    void invoke(UINT id, POINT point) noexcept;

    void reset() noexcept;

private:
    bool populate() noexcept;

    HMENU submenu_{};
    std::vector<std::wstring> paths_;
    HWND owner_{};
    bool extended_{false};
    bool populated_{false};
    IContextMenu* menu_{};
    IContextMenu2* menu2_{};
    IContextMenu3* menu3_{};
};

} // namespace filetree::actions
