#pragma once

// The list-box editors on the Preferences page: the context menu layout (Menu tab), the
// favourites (Favourites tab) and the hidden folders (Folders tab). Each edits a copy the page reads back in from_controls(); the
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

//! Controls of one path list. `up` / `down` 0: the list has no order (no move buttons).
struct PathListIds {
    int list{};
    int remove{};
    int up{};
    int down{};
};

//! A list of folders: the favourites (ordered), the hidden folders. With `files_heading`, a
//! second group follows under that heading (the favourite files); each group keeps its own
//! order and entries only move within their group.
class PathListEditor {
public:
    PathListEditor(const HWND& page, PathListIds ids, const wchar_t* files_heading = nullptr) noexcept
        : page_(page), ids_(ids), files_heading_(files_heading) {}

    std::vector<std::wstring> paths;
    std::vector<std::wstring> files; //!< the second group (shown only with a heading)

    //! `select` is a row: paths, then the heading and the files.
    void show(int select);
    //! The selected row, or -1.
    [[nodiscard]] int selection() const noexcept;
    //! The selected folder or file; null for none or the heading.
    [[nodiscard]] const std::wstring* selected_path() const noexcept;
    void on_select() noexcept;
    //! Asks for a folder and appends it (a repeat is dropped).
    bool add();
    //! Appends those of `extra` that are not listed yet.
    bool merge(const std::vector<std::wstring>& extra);
    //! Replaces the whole list (Restore defaults).
    bool replace(std::vector<std::wstring> next);
    bool remove();
    bool move(bool up);

private:
    struct Spot {
        std::vector<std::wstring>* list{};
        int index{-1};
    };
    [[nodiscard]] bool grouped() const noexcept { return files_heading_ != nullptr && !files.empty(); }
    [[nodiscard]] std::size_t rows() const noexcept {
        return paths.size() + (grouped() ? files.size() + 1 : 0);
    }
    [[nodiscard]] Spot spot(int row) noexcept;
    [[nodiscard]] int row_of(const std::vector<std::wstring>* list, int index) const noexcept {
        return list == &paths ? index : static_cast<int>(paths.size()) + 1 + index;
    }

    const HWND& page_;
    PathListIds ids_;
    const wchar_t* files_heading_{};
};

//! Fills a list box with `items`, sets the horizontal scroll extent to the widest and selects
//! `select` (clamped).
void fill_list(HWND list, const std::vector<std::wstring>& items, int select);

} // namespace filetree::prefs
