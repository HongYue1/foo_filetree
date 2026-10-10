// TreeView inline rename: an EDIT control over the row's text, like Explorer's F2.
// Enter commits; Esc, focus loss, scrolling or any change to the rows cancels.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <commctrl.h>
#include <uxtheme.h>

#include "../actions/shell_ops.h"
#include "edit_util.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace filetree::view {
namespace {

constexpr UINT_PTR edit_subclass_id = 1;

} // namespace

void TreeView::begin_rename(std::uint32_t node) noexcept {
    if (wnd_ == nullptr || refuse_change()) return;
    if (edit_ != nullptr) end_rename(false);
    const model::Node& n = tree_.node(node);
    const auto row = tree_.row_of(node);
    if (n.has(model::node_root) || n.has(model::node_pinned) || !row) {
        MessageBeep(MB_ICONWARNING); // a favourite file's entry would point nowhere after it
        return;
    }
    select_row(*row);

    const int pad = MulDiv(2, metrics_.dpi, 96);
    const RECT rect = rename_rect(*row);
    const std::wstring name(n.name_view());

    edit_ = CreateWindowExW(0, WC_EDITW, name.c_str(),
                            WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_LEFT, rect.left, rect.top,
                            rect.right - rect.left, rect.bottom - rect.top, wnd_, nullptr,
                            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(wnd_, GWLP_HINSTANCE)),
                            nullptr);
    if (edit_ == nullptr) return;
    edit_node_ = node;
    if (colours_.dark) SetWindowTheme(edit_, L"DarkMode_CFD", nullptr);
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(pad, pad));
    SendMessageW(edit_, EM_LIMITTEXT, 255, 0);
    // Like Explorer: a file's name is selected without its extension.
    std::size_t stem = name.size();
    if (!n.has(model::node_container)) {
        if (const auto dot = name.rfind(L'.'); dot != std::wstring::npos && dot > 0) stem = dot;
    }
    SendMessageW(edit_, EM_SETSEL, 0, static_cast<LPARAM>(stem));
    SetWindowSubclass(edit_, edit_proc, edit_subclass_id, reinterpret_cast<DWORD_PTR>(this));
    ShowWindow(edit_, SW_SHOW);
    SetFocus(edit_);
}

RECT TreeView::rename_rect(std::size_t row) const noexcept {
    const model::Node& n = tree_.node(tree_.node_at_row(row));
    const int pad = MulDiv(2, metrics_.dpi, 96);
    const int left = std::max(text_left(n.depth) - pad - 1, 0);
    const int top = row_top(row);
    const int width = std::max(client_width_ - left - pad, metrics_.indent * 4);
    return {left, top, left + width, top + metrics_.row_height};
}

void TreeView::follow_rename() noexcept {
    if (edit_ == nullptr) return;
    const auto row =
        edit_node_ < tree_.node_count() ? tree_.row_of(edit_node_) : std::optional<std::size_t>{};
    if (!row || *row < top_row_ ||
        *row >= top_row_ + static_cast<std::size_t>(std::max(visible_rows(), 1))) {
        end_rename(false);
        return;
    }
    const RECT rect = rename_rect(*row);
    SetWindowPos(edit_, nullptr, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void TreeView::end_rename(bool commit) noexcept {
    HWND edit = edit_;
    if (edit == nullptr) return;
    const std::uint32_t node = edit_node_;
    // Clear first: moving the focus below sends WM_KILLFOCUS, which re-enters here.
    edit_ = nullptr;
    edit_node_ = model::no_node;

    std::wstring text;
    if (commit) {
        const int length = GetWindowTextLengthW(edit);
        text.resize(static_cast<std::size_t>(std::max(length, 0)) + 1);
        text.resize(static_cast<std::size_t>(
            GetWindowTextW(edit, text.data(), static_cast<int>(text.size()))));
    }
    if (GetFocus() == edit && wnd_ != nullptr) SetFocus(wnd_);
    DestroyWindow(edit);
    if (!commit || node >= tree_.node_count()) return;

    try {
        const model::Node& n = tree_.node(node);
        // Windows drops trailing spaces and dots from names; do it here so "unchanged" is right.
        while (!text.empty() && (text.back() == L' ' || text.back() == L'.')) text.pop_back();
        const std::wstring old_name(n.name_view());
        if (text.empty() || text == old_name || n.parent == model::no_node) return;

        std::wstring path;
        tree_.build_path(node, path);
        const std::uint32_t parent = n.parent;
        actions::rename_path(std::move(path), text, wnd_,
                             guard([parent, text, old_name](TreeView& view,
                                                            actions::ShellResult result) {
                                 if (!result.ran) return;
                                 if (result.succeeded) {
                                     UndoRecord record;
                                     record.kind = UndoRecord::Kind::rename;
                                     record.folder = parent;
                                     view.tree_.build_path(parent, record.folder_path);
                                     record.name = text;
                                     record.old_name = old_name;
                                     view.undo_ = std::move(record);
                                 }
                                 view.reload_and_select(parent, text, old_name);
                             }));
    } catch (...) {
    }
}

bool TreeView::on_edit_colour(HDC dc, HWND control, LRESULT& result) noexcept {
    if (control == nullptr || control != edit_) return false;
    SetTextColor(dc, colours_.text);
    SetBkColor(dc, colours_.background);
    SetDCBrushColor(dc, colours_.background);
    result = reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    return true;
}

LRESULT CALLBACK TreeView::edit_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR,
                                     DWORD_PTR data) noexcept {
    auto* view = reinterpret_cast<TreeView*>(data);
    if (edit::ctrl_backspace(wnd, msg, wp)) return 0;
    switch (msg) {
    case WM_GETDLGCODE:
        // Enter and Esc must reach us, not the host's dialog navigation.
        return DefSubclassProc(wnd, msg, wp, lp) | DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_ESCAPE) {
            view->end_rename(wp == VK_RETURN);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == L'\r' || wp == 0x1b) return 0; // no beep
        break;
    case WM_KILLFOCUS:
        if (view->edit_ == wnd) {
            view->end_rename(false);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(wnd, edit_proc, edit_subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(wnd, msg, wp, lp);
}

} // namespace filetree::view

