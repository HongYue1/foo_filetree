#include "list_editors.h"

#include <algorithm>

#include "../../resource.h"
#include "prefs_util.h"

namespace filetree::prefs {

void fill_list(HWND list, const std::vector<std::wstring>& items, int select) {
    if (list == nullptr) return;
    ::SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int widest = 0;
    HDC dc = ::GetDC(list);
    const auto font = reinterpret_cast<HGDIOBJ>(::SendMessageW(list, WM_GETFONT, 0, 0));
    const HGDIOBJ old = ::SelectObject(dc, font);
    for (const std::wstring& item : items) {
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        SIZE size{};
        ::GetTextExtentPoint32W(dc, item.c_str(), static_cast<int>(item.size()), &size);
        widest = std::max(widest, static_cast<int>(size.cx));
    }
    ::SelectObject(dc, old);
    ::ReleaseDC(list, dc);
    ::SendMessageW(list, LB_SETHORIZONTALEXTENT, static_cast<WPARAM>(widest + 8), 0);
    select = std::min(select, static_cast<int>(items.size()) - 1);
    ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
    ::SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    ::InvalidateRect(list, nullptr, TRUE);
}

namespace {

int list_selection(HWND page, int id, std::size_t count) noexcept {
    const auto index = static_cast<int>(::SendMessageW(find_control(page, id), LB_GETCURSEL, 0, 0));
    return index >= 0 && index < static_cast<int>(count) ? index : -1;
}

} // namespace

// --- Menu ---

void MenuEditor::show(int select) {
    std::vector<std::wstring> items;
    for (const settings::MenuItem item : layout.order) {
        std::wstring text = settings::menu_item_label(item);
        if (!layout.visible(item)) text += L"   (hidden)";
        items.push_back(std::move(text));
    }
    fill_list(find_control(page_, IDC_MENU_LIST), items, select);
    on_select();
}

int MenuEditor::selection() const noexcept {
    return list_selection(page_, IDC_MENU_LIST, settings::menu_item_count);
}

void MenuEditor::on_select() noexcept {
    const int index = selection();
    // BM_SETCHECK sends no BN_CLICKED, so this does not mark the page changed.
    set_check(page_, IDC_MENU_SHOW,
              index >= 0 && layout.visible(layout.order[static_cast<std::size_t>(index)]));
    enable(page_, IDC_MENU_SHOW, index >= 0);
    enable(page_, IDC_MENU_UP, index > 0);
    enable(page_, IDC_MENU_DOWN, index >= 0 && index + 1 < int(settings::menu_item_count));
}

bool MenuEditor::move(bool up) {
    const int index = selection();
    const int target = up ? index - 1 : index + 1;
    if (index < 0 || target < 0 || target >= int(settings::menu_item_count)) return false;
    std::swap(layout.order[static_cast<std::size_t>(index)],
              layout.order[static_cast<std::size_t>(target)]);
    show(target);
    return true;
}

bool MenuEditor::toggle_shown() {
    const int index = selection();
    if (index < 0) return false;
    const auto bit = 1u << static_cast<unsigned>(layout.order[static_cast<std::size_t>(index)]);
    layout.hidden = get_check(page_, IDC_MENU_SHOW) ? layout.hidden & ~bit : layout.hidden | bit;
    show(index);
    return true;
}

// --- Path lists ---

void PathListEditor::show(int select) {
    fill_list(find_control(page_, ids_.list), paths, select);
    on_select();
}

int PathListEditor::selection() const noexcept {
    return list_selection(page_, ids_.list, paths.size());
}

void PathListEditor::on_select() noexcept {
    const int index = selection();
    enable(page_, ids_.remove, index >= 0);
    if (ids_.up == 0) return;
    enable(page_, ids_.up, index > 0);
    enable(page_, ids_.down, index >= 0 && index + 1 < static_cast<int>(paths.size()));
}

bool PathListEditor::add() {
    std::wstring path;
    if (!pick_folder(page_, path)) return false;
    if (!merge({path})) return false;
    show(static_cast<int>(paths.size()) - 1);
    return true;
}

bool PathListEditor::merge(const std::vector<std::wstring>& extra) {
    std::vector<std::wstring> next = paths;
    next.insert(next.end(), extra.begin(), extra.end());
    next = settings::split_paths(settings::join_paths(next)); // cleans, drops repeats
    if (next == paths) return false;
    paths = std::move(next);
    show(std::max(selection(), 0));
    return true;
}

bool PathListEditor::remove() {
    const int index = selection();
    if (index < 0) return false;
    paths.erase(paths.begin() + index);
    show(index);
    return true;
}

bool PathListEditor::move(bool up) {
    const int index = selection();
    const int target = up ? index - 1 : index + 1;
    if (ids_.up == 0 || index < 0 || target < 0 || target >= static_cast<int>(paths.size())) {
        return false;
    }
    std::swap(paths[static_cast<std::size_t>(index)], paths[static_cast<std::size_t>(target)]);
    show(target);
    return true;
}

} // namespace filetree::prefs
