// TreeView painting. Only rows that intersect the invalid rectangle are drawn, into one cached
// DIB, then copied to the window in one BitBlt. Nothing here allocates: colours go through the
// stock DC brush and pen, text through DrawTextW on the node's pooled name.

#include "tree_view.h"

#include <uxtheme.h>

#include "icon_font.h"
#include "../model/file_kind.h"

#pragma comment(lib, "uxtheme.lib")

#include <algorithm>

namespace filetree::view {
namespace {

void fill(HDC dc, const RECT& rect, COLORREF colour) noexcept {
    SetDCBrushColor(dc, colour);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

//! A small solid triangle: pointing right (collapsed) or down (expanded), centred on (cx, cy).
void draw_expander(HDC dc, int cx, int cy, int size, bool expanded, COLORREF colour) noexcept {
    const int half = size / 2;
    const int quarter = std::max(size / 4, 1);
    POINT points[3];
    if (expanded) {
        points[0] = {cx - half, cy - quarter};
        points[1] = {cx + half, cy - quarter};
        points[2] = {cx, cy + quarter + 1};
    } else {
        points[0] = {cx - quarter, cy - half};
        points[1] = {cx - quarter, cy + half};
        points[2] = {cx + quarter + 1, cy};
    }
    SetDCBrushColor(dc, colour);
    SetDCPenColor(dc, colour);
    Polygon(dc, points, 3);
}

constexpr wchar_t failed_suffix[] = L"  (unavailable)";

} // namespace

void TreeView::ensure_buffer(int width, int height) noexcept {
    if (wnd_ == nullptr || width <= 0 || height <= 0) return;
    if (buffer_dc_ != nullptr && width <= buffer_width_ && height <= buffer_height_) return;

    // Grow only, with some slack, so dragging a splitter does not reallocate on every WM_SIZE.
    const int new_width = std::max(width + 64, buffer_width_);
    const int new_height = std::max(height + 64, buffer_height_);
    release_buffer();

    HDC screen = GetDC(wnd_);
    buffer_dc_ = CreateCompatibleDC(screen);
    buffer_bitmap_ = CreateCompatibleBitmap(screen, new_width, new_height);
    ReleaseDC(wnd_, screen);
    if (buffer_dc_ == nullptr || buffer_bitmap_ == nullptr) {
        release_buffer();
        return;
    }
    buffer_old_bitmap_ = SelectObject(buffer_dc_, buffer_bitmap_);
    SelectObject(buffer_dc_, GetStockObject(DC_BRUSH));
    SelectObject(buffer_dc_, GetStockObject(DC_PEN));
    SetBkMode(buffer_dc_, TRANSPARENT);
    buffer_width_ = new_width;
    buffer_height_ = new_height;
}

void TreeView::release_buffer() noexcept {
    if (buffer_dc_ != nullptr) {
        if (buffer_old_bitmap_ != nullptr) SelectObject(buffer_dc_, buffer_old_bitmap_);
        DeleteDC(buffer_dc_);
    }
    if (buffer_bitmap_ != nullptr) DeleteObject(buffer_bitmap_);
    buffer_dc_ = nullptr;
    buffer_bitmap_ = nullptr;
    buffer_old_bitmap_ = nullptr;
    buffer_width_ = buffer_height_ = 0;
}

void TreeView::paint(HDC target, const RECT& dirty) noexcept {
    if (client_width_ <= 0 || client_height_ <= 0) return;
    ensure_buffer(client_width_, client_height_);
    // No buffer (out of GDI resources): draw straight to the window rather than not at all.
    HDC dc = buffer_dc_ != nullptr ? buffer_dc_ : target;
    if (dc == target) {
        SelectObject(dc, GetStockObject(DC_BRUSH));
        SelectObject(dc, GetStockObject(DC_PEN));
        SetBkMode(dc, TRANSPARENT);
    }
    const HGDIOBJ old_font = SelectObject(dc, font_ != nullptr ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    if (transparent_) {
        // What is behind the panel: the host forwards this to its own parent (Panel). The
        // background colour first, in case no window up the chain paints anything.
        fill(dc, dirty, colours_.background);
        DrawThemeParentBackground(wnd_, dc, &dirty);
    }

    const int row_height = metrics_.row_height;
    const std::size_t rows = tree_.row_count();
    const auto boundary = boundary_row();
    const bool gap_shown = boundary && *boundary >= top_row_;

    // Rows from the first one the dirty rectangle touches (the gap shifts those below it).
    std::size_t row = top_row_ + static_cast<std::size_t>(std::max<int>(dirty.top, 0) / row_height);
    if (gap_shown && row > *boundary) {
        row = std::max(*boundary, top_row_ + static_cast<std::size_t>(
                                                  std::max<int>(dirty.top - metrics_.group_gap, 0) /
                                                  row_height));
    }
    int bottom = std::max<int>(dirty.top, 0);
    for (; row < rows; ++row) {
        const int top = row_top(row);
        const int gap_top = gap_shown && row == *boundary ? top - metrics_.group_gap : top;
        if (gap_top >= dirty.bottom) break;
        if (gap_shown && row == *boundary) {
            // The gap above the second group, with the optional line through its middle.
            const int gap = metrics_.group_gap;
            const RECT gap_rect{0, top - gap, client_width_, top};
            if (gap > 0 && !transparent_) fill(dc, gap_rect, colours_.background);
            if (separate_favourites_) {
                const int margin = MulDiv(4, metrics_.dpi, 96);
                const int thickness = std::max(metrics_.line_width, 1);
                const int y = gap > 0 ? top - (gap + thickness) / 2 : top;
                fill(dc, RECT{margin, y, client_width_ - margin, y + thickness}, line_colour_);
            }
        }
        const RECT rect{0, top, client_width_, top + row_height};
        if (rect.bottom > dirty.top) {
            paint_row(dc, row, rect);
            if (gap_shown && row == *boundary && separate_favourites_ && metrics_.group_gap == 0) {
                const int margin = MulDiv(4, metrics_.dpi, 96);
                const int thickness = std::max(metrics_.line_width, 1);
                fill(dc, RECT{margin, top, client_width_ - margin, top + thickness}, line_colour_);
            }
        }
        bottom = rect.bottom;
    }
    if (bottom < dirty.bottom && !transparent_) {
        fill(dc, RECT{dirty.left, bottom, dirty.right, dirty.bottom}, colours_.background);
    }
    if (tree_.node_count() == 0) {
        // No roots at all: drives switched off and no favourites (or library folders) yet.
        const int margin = MulDiv(12, metrics_.dpi, 96);
        RECT hint{margin, margin, client_width_ - margin, client_height_ - margin};
        SetTextColor(dc, dim_text_);
        DrawTextW(dc,
                  L"Nothing to show yet. Right-click here and open Preferences to add favourite "
                  L"folders or show the drives.",
                  -1, &hint, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    }

    SelectObject(dc, old_font);
    if (dc != target) {
        BitBlt(target, dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top,
               dc, dirty.left, dirty.top, SRCCOPY);
    }
}

void TreeView::paint_row(HDC dc, std::size_t row, const RECT& rect) noexcept {
    const std::uint32_t index = tree_.node_at_row(row);
    const model::Node& node = tree_.node(index);
    const bool selected = node.has(model::node_selected);
    const bool focus = static_cast<std::ptrdiff_t>(row) == selected_row_;
    const bool hovered = (hover_highlight_ && static_cast<std::ptrdiff_t>(row) == hover_row_) ||
                         static_cast<std::ptrdiff_t>(row) == drop_row_;

    COLORREF background = zebra_ && row % 2 == 1 ? zebra_background_ : colours_.background;
    COLORREF text = colours_.text;
    COLORREF glyph = expander_colour_;
    if (selected) {
        background = focused_ ? colours_.selection_background : colours_.inactive_selection_background;
        text = focused_ ? colours_.selection_text : colours_.inactive_selection_text;
        glyph = text;
    } else {
        if (hovered) background = hover_background_;
        if (is_cut(index)) text = dim_text_; // cut, as Explorer ghosts it
    }
    if (!transparent_ || background != colours_.background) fill(dc, rect, background);
    // While the keyboard drives a multi-selection (or after Ctrl+Space unselected the focus
    // row), the focus row gets a soft inset frame so the keyboard position stays visible.
    if (focus && focused_ && keyboard_cue_ && (!selected || tree_.selection_hint() > 1)) {
        SetDCBrushColor(dc, selected ? blend(background, text, 0.45)
                                     : blend(background, colours_.text, 0.35));
        RECT frame{rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1};
        FrameRect(dc, &frame, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }

    const bool expandable = node.has(model::node_container) &&
                            !(node.has(model::node_loaded) && node.child_count == 0);
    if (lines_ != settings::TreeLines::none) paint_lines(dc, index, rect, expandable);


    // Expander: folders that might have children. A loaded empty folder has none to show.
    if (expandable) {
        const bool open = node.has(model::node_expanded) && !node.has(model::node_load_failed);
        const int cx = expander_left(node.depth) + metrics_.indent / 2;
        const int cy = (rect.top + rect.bottom) / 2;
        const COLORREF colour = node.has(model::node_loading) ? dim_text_ : glyph;
        draw_expander(dc, cx, cy, metrics_.expander, open, colour);
    }

    const int playing = playing_mark(index);
    if (metrics_.icon_width > 0) {
        const bool strong = selected || is_cut(index) || playing == 2;
        paint_icon(dc, node, rect, strong ? text : icon_colour_, playing == 2);
    }

    const std::wstring_view name = shown_name(node);

    RECT text_rect{text_left(node.depth), rect.top, rect.right - metrics_.text_gap, rect.bottom};
    if (text_rect.left >= text_rect.right) return;
    SetTextColor(dc, text);
    constexpr UINT format = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;

    // Marks after the name: the favourite star, the now playing triangle. A long name gives up
    // room for them.
    const bool icon_font = icon_font_ != nullptr;
    wchar_t marks[2];
    COLORREF mark_colours[2];
    HFONT mark_fonts[2];
    int mark_tops[2];
    int mark_count = 0;
    if (mark_favourites_ && node.has(model::node_container) && is_favourite(index)) {
        marks[mark_count] = icon_font ? glyph::star : glyph::star_fallback;
        mark_fonts[mark_count] = icon_font && mark_font_ != nullptr ? mark_font_ : font_;
        mark_tops[mark_count] = metrics_.star_top;
        mark_colours[mark_count++] = selected ? text : icon_colour_;
    }
    // With icons, the playing file's icon is the triangle (paint_icon); a closed folder holding
    // it, or any row without icons, gets it after the name.
    if (playing != 0 && (playing == 1 || metrics_.icon_width == 0)) {
        marks[mark_count] = icon_font ? glyph::playing : glyph::playing_fallback;
        mark_fonts[mark_count] = icon_font && mark_font_ != nullptr ? mark_font_ : font_;
        mark_tops[mark_count] = metrics_.play_top;
        // The file itself in the text colour, a closed folder holding it dimmer.
        mark_colours[mark_count++] = selected || playing == 2 ? text : icon_colour_;
    }
    if (mark_count > 0) {
        const int gap = MulDiv(4, metrics_.dpi, 96);
        SIZE extent{};
        GetTextExtentPoint32W(dc, name.data(), static_cast<int>(name.size()), &extent);
        int marks_width = 0;
        int widths[2]{};
        for (int i = 0; i < mark_count; ++i) {
            SIZE size{};
            SelectObject(dc, mark_fonts[i]);
            GetTextExtentPoint32W(dc, &marks[i], 1, &size);
            widths[i] = size.cx;
            marks_width += size.cx + (i > 0 ? gap : 0);
        }
        SelectObject(dc, font_);
        const int marks_left = std::min<int>(text_rect.left + extent.cx + gap,
                                             text_rect.right - marks_width);
        RECT name_rect = text_rect;
        name_rect.right = std::max<int>(marks_left - gap, name_rect.left);
        DrawTextW(dc, name.data(), static_cast<int>(name.size()), &name_rect, format);
        if (marks_left > text_rect.left) {
            int left = marks_left;
            for (int i = 0; i < mark_count; ++i) {
                // Placed by measure_marks (ink centred on the text), so no DT_VCENTER.
                RECT mark_rect{left, rect.top + mark_tops[i], text_rect.right,
                               rect.bottom + metrics_.row_height};
                SelectObject(dc, mark_fonts[i]);
                SetTextColor(dc, mark_colours[i]);
                DrawTextW(dc, &marks[i], 1, &mark_rect, DT_SINGLELINE | DT_NOPREFIX);
                left += widths[i] + gap;
            }
            SelectObject(dc, font_);
        }
        return;
    }
    DrawTextW(dc, name.data(), static_cast<int>(name.size()), &text_rect, format);

    if (node.has(model::node_load_failed)) {
        SIZE extent{};
        GetTextExtentPoint32W(dc, name.data(), static_cast<int>(name.size()), &extent);
        RECT suffix_rect = text_rect;
        suffix_rect.left += extent.cx;
        if (suffix_rect.left < suffix_rect.right) {
            SetTextColor(dc, selected ? text : dim_text_);
            DrawTextW(dc, failed_suffix, static_cast<int>(std::size(failed_suffix) - 1),
                      &suffix_rect, format);
        }
    }
}

void TreeView::paint_lines(HDC dc, std::uint32_t index, const RECT& rect, bool expandable) noexcept {
    const model::Node& node = tree_.node(index);
    const int w = metrics_.line_width;
    const int half = w / 2;
    const auto column = [&](std::uint16_t depth) {
        return expander_left(depth) + metrics_.indent / 2 - half;
    };
    const auto bar = [&](int left, int top, int right, int bottom) {
        if (right > left && bottom > top) fill(dc, RECT{left, top, right, bottom}, line_colour_);
    };
    const auto has_next_sibling = [&](std::uint32_t n) {
        const model::Node& item = tree_.node(n);
        if (item.parent == model::no_node) return false;
        const model::Node& parent = tree_.node(item.parent);
        return n + 1 < parent.first_child + parent.child_count;
    };
    const int mid = (rect.top + rect.bottom) / 2 - half;
    const int glyph_half = metrics_.expander / 2 + 1;
    const bool open_with_children = expandable && node.has(model::node_expanded) &&
                                    node.has(model::node_loaded) &&
                                    !node.has(model::node_load_failed);

    if (lines_ == settings::TreeLines::guides) {
        // One vertical guide per ancestor level, through the whole row.
        for (std::uint16_t level = 0; level < node.depth; ++level) {
            const int x = column(level);
            bar(x, rect.top, x + w, rect.bottom);
        }
        return;
    }

    // Connectors. Roots (drives) have no parent to connect to.
    if (node.depth > 0) {
        const int x = column(static_cast<std::uint16_t>(node.depth - 1));
        bar(x, rect.top, x + w, has_next_sibling(index) ? rect.bottom : mid + w);
        const int end = expandable ? column(node.depth) + half - glyph_half
                                   : content_left(node.depth) - metrics_.text_gap;
        bar(x, mid, end, mid + w);
        // Ancestors that still have siblings below continue their line through this row.
        std::uint32_t walk = node.parent;
        while (walk != model::no_node && tree_.node(walk).depth > 0) {
            if (has_next_sibling(walk)) {
                const int ax = column(static_cast<std::uint16_t>(tree_.node(walk).depth - 1));
                bar(ax, rect.top, ax + w, rect.bottom);
            }
            walk = tree_.node(walk).parent;
        }
    }
    // An open folder's line starts under its expander and continues into its first child.
    if (open_with_children && node.child_count > 0) {
        const int x = column(node.depth);
        bar(x, mid + half + glyph_half, x + w, rect.bottom);
    }
}

void TreeView::paint_icon(HDC dc, const model::Node& node, const RECT& rect, COLORREF colour,
                          bool playing) noexcept {
    wchar_t icon = glyph::document;
    if (node.has(model::node_root) && !node.has(model::node_favourite)) {
        icon = glyph::drive;
    } else if (node.has(model::node_container)) {
        const bool open = node.has(model::node_expanded) && node.has(model::node_loaded) &&
                          node.child_count > 0;
        icon = open ? glyph::folder_open : glyph::folder;
    } else if (playing) {
        icon = glyph::playing;
    } else {
        const std::wstring_view extension = model::extension_of(node.name_view());
        switch (model::file_kind(extension)) {
        case model::FileKind::video: icon = glyph::video; break;
        case model::FileKind::image: icon = glyph::image; break;
        case model::FileKind::text: icon = glyph::text; break;
        case model::FileKind::pdf: icon = glyph::pdf; break;
        case model::FileKind::playlist: icon = glyph::playlist; break;
        case model::FileKind::other:
            if (playable_ != nullptr && !extension.empty() && playable_->contains(extension)) {
                icon = glyph::audio;
            }
            break;
        }
    }
    const int left = content_left(node.depth);
    RECT box{left, rect.top, left + metrics_.icon, rect.bottom};
    SelectObject(dc, icon_font_);
    SetTextColor(dc, colour);
    DrawTextW(dc, &icon, 1, &box, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    SelectObject(dc, font_);
}

bool TreeView::is_favourite(std::uint32_t index) noexcept {
    const model::Node& node = tree_.node(index);
    if (node.has(model::node_favourite)) return true;
    if (favourite_paths_.empty()) return false;
    try {
        // The last component first: building the full path is only needed on a name match.
        std::wstring_view name = node.name_view();
        while (name.size() > 1 && name.back() == L'\\') name.remove_suffix(1);
        favourite_scratch_.assign(name);
        CharUpperBuffW(favourite_scratch_.data(), static_cast<DWORD>(favourite_scratch_.size()));
        if (favourite_leaves_.find(favourite_scratch_) == favourite_leaves_.end()) return false;
        tree_.build_path(index, favourite_scratch_);
        while (favourite_scratch_.size() > 1 && favourite_scratch_.back() == L'\\') {
            favourite_scratch_.pop_back();
        }
        CharUpperBuffW(favourite_scratch_.data(), static_cast<DWORD>(favourite_scratch_.size()));
        return favourite_paths_.find(favourite_scratch_) != favourite_paths_.end();
    } catch (...) {
        return false;
    }
}

} // namespace filetree::view
