#pragma once

// The list-box editors on the Preferences page: the context menu layout (Menu tab) and the
// favourites (Favourites tab). Each edits a copy the page reads back in from_controls(); the
// mutators return true when something changed, so the page can mark itself changed.

#include <windows.h>

#include <string>
#include <vector>

#include "../settings/settings_model.h"

namespace filetree::prefs {

class MenuEditor {
public:
    //! `page` is the page's m_hWnd, read on use (the editor is made before the window).
    explicit MenuEditor(const HWND& page) noexcept : page_(page) {}

    settings::MenuLayout layout{settings::MenuLayout::defaults()};

    void show(int select);
    [[nodiscard]] int selection() const noexcept;
    //! Syncs the Show check box and the Up / Down buttons with the selected entry.
    void on_select() noexcept;
    bool move(bool up);
    //! The Show check box was clicked.
    bool toggle_shown();

private:
    const HWND& page_;
};

class FavouritesEditor {
public:
    explicit FavouritesEditor(const HWND& page) noexcept : page_(page) {}

    std::vector<std::wstring> paths;

    void show(int select);
    [[nodiscard]] int selection() const noexcept;
    void on_select() noexcept;
    //! Asks for a folder and appends it (a repeat is dropped).
    bool add();
    bool remove();
    bool move(bool up);

private:
    const HWND& page_;
};

//! Fills a list box with `items`, sets the horizontal scroll extent to the widest and selects
//! `select` (clamped).
void fill_list(HWND list, const std::vector<std::wstring>& items, int select);

} // namespace filetree::prefs
