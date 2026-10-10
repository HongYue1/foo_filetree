// TreeView: the thin scrollbar (settings thin_scrollbar). Instead of the system bar, a narrow
// thumb is drawn over the right edge of the rows while the mouse is over the tree (or the thumb
// is being dragged). It widens under the mouse; dragging it scrolls, clicking beside it pages.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <algorithm>

namespace filetree::view {

bool TreeView::thin_shown() const noexcept {
    return thin_scrollbar_ && (mouse_inside_ || thumb_drag_ >= 0) && max_top_row() > 0 &&
           client_height_ > 0;
}

RECT TreeView::thin_track() const noexcept {
    const int width = MulDiv(thumb_hot_ || thumb_drag_ >= 0 ? 8 : 4, metrics_.dpi, 96);
    const int margin = MulDiv(2, metrics_.dpi, 96);
    return {client_width_ - margin - width, 0, client_width_ - margin, client_height_};
}

RECT TreeView::thin_thumb() const noexcept {
    RECT rect = thin_track();
    const std::size_t rows = std::max<std::size_t>(tree_.row_count(), 1);
    const std::size_t max_top = std::max<std::size_t>(max_top_row(), 1);
    const int height = client_height_;
    const int visible = std::max(visible_rows(), 1);
    const int min_thumb = MulDiv(20, metrics_.dpi, 96);
    const int thumb = std::clamp(static_cast<int>(static_cast<double>(height) * visible / rows),
                                 std::min(min_thumb, height), height);
    const int top = static_cast<int>(static_cast<double>(height - thumb) *
                                     static_cast<double>(std::min(top_row_, max_top)) / max_top);
    rect.top = top;
    rect.bottom = top + thumb;
    return rect;
}

//! The whole strip the thumb can occupy at its widest.
void TreeView::invalidate_thin() noexcept {
    if (wnd_ == nullptr || !thin_scrollbar_) return;
    const RECT strip{client_width_ - MulDiv(10, metrics_.dpi, 96), 0, client_width_, client_height_};
    InvalidateRect(wnd_, &strip, FALSE);
}

void TreeView::paint_thin_scrollbar(HDC dc) noexcept {
    if (!thin_shown()) return;
    const bool hot = thumb_hot_ || thumb_drag_ >= 0;
    const COLORREF colour = blend(colours_.background, colours_.text, hot ? 0.55 : 0.35);
    const RECT thumb = thin_thumb();
    const int round = thumb.right - thumb.left;
    SetDCBrushColor(dc, colour);
    SetDCPenColor(dc, colour);
    const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    const HGDIOBJ old_pen = SelectObject(dc, GetStockObject(DC_PEN));
    RoundRect(dc, thumb.left, thumb.top, thumb.right, thumb.bottom, round, round);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
}

void TreeView::set_thin_hot(bool hot) noexcept {
    if (hot == thumb_hot_) return;
    thumb_hot_ = hot;
    invalidate_thin();
}

void TreeView::on_thin_mouse_move(int x, int y) noexcept {
    if (!thin_scrollbar_) return;
    if (!mouse_inside_) {
        mouse_inside_ = true;
        invalidate_thin();
    }
    if (thumb_drag_ >= 0) {
        // The thumb's top follows the mouse; the row at that fraction becomes the top row.
        const RECT thumb = thin_thumb();
        const int range = client_height_ - (thumb.bottom - thumb.top);
        if (range <= 0) return;
        const double at = std::clamp(static_cast<double>(y - thumb_drag_) / range, 0.0, 1.0);
        restore_top_node_ = model::no_node; // the user scrolls
        scroll_to(static_cast<std::size_t>(at * static_cast<double>(max_top_row()) + 0.5));
        return;
    }
    set_thin_hot(thin_shown() && x >= client_width_ - MulDiv(10, metrics_.dpi, 96));
}

void TreeView::on_thin_mouse_leave() noexcept {
    if (!thin_scrollbar_ || thumb_drag_ >= 0) return;
    mouse_inside_ = false;
    thumb_hot_ = false;
    invalidate_thin();
}

bool TreeView::on_thin_button_down(int x, int y) noexcept {
    if (!thin_shown() || x < client_width_ - MulDiv(10, metrics_.dpi, 96)) return false;
    const RECT thumb = thin_thumb();
    if (y >= thumb.top && y < thumb.bottom) {
        thumb_drag_ = y - thumb.top;
        SetCapture(wnd_);
        invalidate_thin();
    } else {
        on_vscroll(y < thumb.top ? SB_PAGEUP : SB_PAGEDOWN);
    }
    return true;
}

bool TreeView::end_thin_drag() noexcept {
    if (thumb_drag_ < 0) return false;
    thumb_drag_ = -1;
    if (GetCapture() == wnd_) ReleaseCapture();
    // Still over the tree? Otherwise the leave was held back while dragging.
    POINT cursor{};
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) != wnd_) {
        mouse_inside_ = false;
        thumb_hot_ = false;
    }
    invalidate_thin();
    return true;
}

} // namespace filetree::view
