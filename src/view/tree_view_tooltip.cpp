// TreeView: row tooltips (Preferences > View). "Cut off" shows the full name in place, over the
// row, only when the row cuts it off; "Full path" shows the item's path by the cursor. The tool
// follows the hover row; the text is only built when a tip is about to show.

#include "tree_view.h"

#include <commctrl.h>

namespace filetree::view {

std::wstring_view TreeView::shown_name(const model::Node& node) const noexcept {
    std::wstring_view name = model::display_name(node);
    if (extensions_ != settings::Extensions::always && !node.has(model::node_container)) {
        const std::wstring_view extension = model::extension_of(name);
        const bool hide = !extension.empty() &&
                          (extensions_ == settings::Extensions::never ||
                           (playable_ != nullptr && playable_->contains(extension)));
        if (hide) name.remove_suffix(extension.size() + 1);
    }
    return name;
}

void TreeView::update_tooltip(std::ptrdiff_t row) noexcept {
    tip_row_ = -1;
    if (tooltips_ == settings::Tooltips::off || row < 0 || edit_ != nullptr ||
        static_cast<std::size_t>(row) >= tree_.row_count()) {
        tooltip_.clear();
        return;
    }
    if (tooltip_.wnd() == nullptr) {
        if (!tooltip_.ensure(wnd_)) return;
        tooltip_.set_dark(colours_.dark);
        tooltip_.set_font(font_);
    }
    tip_row_ = row;
    const int top = row_top(static_cast<std::size_t>(row));
    tooltip_.set_area(RECT{0, top, client_width_, top + metrics_.row_height});
}

bool TreeView::text_cut_off(std::size_t row, SIZE& extent) noexcept {
    const std::uint32_t index = tree_.node_at_row(row);
    const model::Node& node = tree_.node(index);
    const std::wstring_view name = shown_name(node);
    HDC dc = GetDC(wnd_);
    if (dc == nullptr) return false;
    const HGDIOBJ old = SelectObject(dc, font_);
    extent = SIZE{};
    GetTextExtentPoint32W(dc, name.data(), static_cast<int>(name.size()), &extent);
    SelectObject(dc, old);
    ReleaseDC(wnd_, dc);

    // The marks after a name take room from it (paint_row): allow for them.
    int room = client_width_ - metrics_.text_gap - text_left(node.depth);
    const int mark = MulDiv(4 + settings::current().mark_size, metrics_.dpi, 96);
    if (mark_favourites_ && node.has(model::node_container) && is_favourite(index)) room -= mark;
    const int playing = playing_mark(index);
    if (playing != 0 && (playing == 1 || metrics_.icon_width == 0)) room -= mark;
    return extent.cx > room;
}

bool TreeView::on_tooltip_notify(const NMHDR& header, LRESULT& result) noexcept {
    if (tooltip_.wnd() == nullptr || header.hwndFrom != tooltip_.wnd()) return false;
    result = 0;
    const bool row_ok = tip_row_ >= 0 && static_cast<std::size_t>(tip_row_) < tree_.row_count();
    if (header.code == TTN_GETDISPINFOW) {
        auto& info = *reinterpret_cast<NMTTDISPINFOW*>(const_cast<NMHDR*>(&header));
        tip_text_.clear();
        tip_in_place_ = false;
        try {
            if (row_ok) {
                const auto row = static_cast<std::size_t>(tip_row_);
                if (tooltips_ == settings::Tooltips::path) {
                    tree_.build_path(tree_.node_at_row(row), tip_text_);
                } else if (SIZE extent{}; text_cut_off(row, extent)) {
                    tip_text_.assign(shown_name(tree_.node(tree_.node_at_row(row))));
                    tip_in_place_ = true;
                }
            }
        } catch (...) {
            tip_text_.clear();
        }
        // Empty text: no tip for this row.
        info.lpszText = tip_text_.data();
        info.hinst = nullptr;
        return true;
    }
    if (header.code == TTN_SHOW && tip_in_place_ && row_ok) {
        // Over the row's text, so the name just continues past the panel's edge.
        const auto row = static_cast<std::size_t>(tip_row_);
        SIZE extent{};
        (void)text_cut_off(row, extent);
        const model::Node& node = tree_.node(tree_.node_at_row(row));
        const int left = text_left(node.depth);
        const int top = row_top(row) + (metrics_.row_height - extent.cy) / 2;
        RECT rect{left, top, left + extent.cx, top + extent.cy};
        MapWindowPoints(wnd_, nullptr, reinterpret_cast<POINT*>(&rect), 2);
        SendMessageW(header.hwndFrom, TTM_ADJUSTRECT, TRUE, reinterpret_cast<LPARAM>(&rect));
        SetWindowPos(header.hwndFrom, nullptr, rect.left, rect.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        result = TRUE;
        return true;
    }
    return false;
}

} // namespace filetree::view
