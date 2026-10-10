// TreeView layout and scrolling: fonts and metrics, row geometry, the scroll bar, size and DPI.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <algorithm>

#include "../platform/dpi.h"
#include "icon_font.h"

namespace filetree::view {
namespace {

UINT window_dpi(HWND wnd) noexcept { return dpi::of_window(wnd); }
int system_dpi() noexcept { return dpi::system(); }
int scale(int dips, int dpi) noexcept { return MulDiv(dips, dpi, 96); }

} // namespace

void TreeView::set_colours(const ViewColours& colours) noexcept {
    colours_ = colours;
    hover_background_ = blend(colours.background, colours.selection_background,
                              colours.dark ? 0.30 : 0.18);
    dim_text_ = blend(colours.text, colours.background, 0.45);
    expander_colour_ = blend(colours.text, colours.background, 0.35);
    icon_colour_ = blend(colours.text, colours.background, 0.2);
    zebra_background_ = blend(colours.background, colours.text, colours.dark ? 0.05 : 0.035);
    tooltip_.set_dark(colours.dark);
    const settings::Settings& s = settings::current();
    line_colour_ = s.line_custom_colour
                       ? s.line_colour
                       : blend(colours.background, colours.text, s.line_opacity / 100.0);
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void TreeView::set_font(const LOGFONTW& font) noexcept {
    base_font_ = font;
    has_base_font_ = true;
    if (wnd_ == nullptr) return;
    remeasure();
    on_size();
    InvalidateRect(wnd_, nullptr, FALSE);
}

void TreeView::rebuild_font() noexcept {
    if (!has_base_font_) {
        // No host font yet: the shell's icon-title font, also at system DPI.
        SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof(base_font_), &base_font_, 0);
    }
    LOGFONTW scaled = base_font_;
    scaled.lfHeight = MulDiv(base_font_.lfHeight, metrics_.dpi, system_dpi());
    if (font_ != nullptr) DeleteObject(font_);
    font_ = CreateFontIndirectW(&scaled);
    tooltip_.set_font(font_);
}

void TreeView::remeasure() noexcept {
    metrics_.dpi = static_cast<int>(window_dpi(wnd_));
    rebuild_font();

    TEXTMETRICW tm{};
    HDC dc = GetDC(wnd_);
    const HGDIOBJ old = SelectObject(dc, font_ != nullptr ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(wnd_, dc);

    const int dpi = metrics_.dpi;
    metrics_.row_height =
        std::max<int>(tm.tmHeight + 2 * scale(settings::current().row_padding, dpi), 1);
    metrics_.line_width = std::max(scale(settings::current().line_thickness, dpi), 1);
    metrics_.indent = scale(16, dpi);
    metrics_.expander = std::max(scale(8, dpi) | 1, 5); // odd, so the glyph has a centre pixel
    metrics_.text_gap = scale(2, dpi);
    metrics_.text_ascent = tm.tmAscent;

    // Icon glyphs: 16 px at 100% (Explorer's small icons), never taller than the text.
    if (icon_font_ != nullptr) {
        DeleteObject(icon_font_);
        icon_font_ = nullptr;
    }
    if (mark_font_ != nullptr) {
        DeleteObject(mark_font_);
        mark_font_ = nullptr;
    }
    metrics_.icon = 0;
    metrics_.icon_width = 0;
    const wchar_t* face = icon_font_face();
    if (face != nullptr && (show_icons_ || mark_favourites_ || mark_playing_)) {
        metrics_.icon = std::min<int>(scale(settings::current().icon_size, dpi),
                                      metrics_.row_height);
        LOGFONTW icon{};
        icon.lfHeight = -metrics_.icon;
        icon.lfCharSet = DEFAULT_CHARSET;
        icon.lfQuality = CLEARTYPE_QUALITY;
        wcsncpy_s(icon.lfFaceName, face, _TRUNCATE);
        icon_font_ = CreateFontIndirectW(&icon);
        // Marks after a name (star, play) are smaller: they decorate text, they are not icons.
        icon.lfHeight = -std::max<int>(scale(settings::current().mark_size, dpi), 1);
        mark_font_ = CreateFontIndirectW(&icon);
        if (icon_font_ == nullptr) metrics_.icon = 0;
    }
    if (show_icons_ && icon_font_ != nullptr) metrics_.icon_width = metrics_.icon + scale(4, dpi);
    measure_marks(tm);
    metrics_.group_gap = scale(settings::current().favourites_gap, dpi);
}

void TreeView::measure_marks(const TEXTMETRICW& text) noexcept {
    // DrawText's DT_VCENTER centres the font's cell, but icon glyphs sit anywhere in it (the play
    // triangle is low). Place each mark by its ink instead: centred on the capitals of the text.
    HDC dc = GetDC(wnd_);
    if (dc == nullptr) return;
    const HGDIOBJ old = SelectObject(dc, font_ != nullptr ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    const MAT2 identity{{0, 1}, {0, 0}, {0, 0}, {0, 1}};
    // The capitals' ink, measured on "H" (font tables' cap height is often missing or off).
    int cap_top = text.tmAscent - text.tmInternalLeading; // above the baseline
    int cap_bottom = 0;
    if (GLYPHMETRICS h{}; GetGlyphOutlineW(dc, L'H', GGO_METRICS, &h, 0, nullptr, &identity) !=
                          GDI_ERROR) {
        cap_top = h.gmptGlyphOrigin.y;
        cap_bottom = h.gmptGlyphOrigin.y - static_cast<int>(h.gmBlackBoxY);
    }
    const int baseline = (metrics_.row_height - text.tmHeight) / 2 + text.tmAscent;
    const int target = baseline - (cap_top + cap_bottom) / 2; // from the row top
    const auto top_for = [&](wchar_t ch, HFONT font) {
        SelectObject(dc, font);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        GLYPHMETRICS gm{};
        int ink_centre = tm.tmAscent / 2; // above the baseline; a guess if the glyph is missing
        if (GetGlyphOutlineW(dc, ch, GGO_METRICS, &gm, 0, nullptr, &identity) != GDI_ERROR) {
            ink_centre = gm.gmptGlyphOrigin.y - static_cast<int>(gm.gmBlackBoxY) / 2;
        }
        // The cell top that puts the ink centre on the target.
        return target - tm.tmAscent + ink_centre;
    };
    const bool icons = icon_font_ != nullptr && mark_font_ != nullptr;
    HFONT text_font = font_ != nullptr ? font_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    metrics_.star_top = top_for(icons ? glyph::star : glyph::star_fallback, icons ? mark_font_ : text_font);
    metrics_.play_top =
        top_for(icons ? glyph::playing : glyph::playing_fallback, icons ? mark_font_ : text_font);
    SelectObject(dc, old);
    ReleaseDC(wnd_, dc);
}

// --- Geometry -------------------------------------------------------------------------------

int TreeView::visible_rows() const noexcept {
    // The boundary gap is subtracted whether or not it is on screen: simple and never too many.
    const int gap = boundary_node_ != model::no_node ? metrics_.group_gap : 0;
    return std::max((client_height_ - gap) / metrics_.row_height, 1);
}

std::optional<std::size_t> TreeView::boundary_row() const noexcept {
    if (boundary_node_ == model::no_node) return std::nullopt;
    // Cached: re-searched only after rows above it changed.
    if (boundary_row_cache_ < tree_.row_count() &&
        tree_.node_at_row(boundary_row_cache_) == boundary_node_) {
        return boundary_row_cache_;
    }
    const auto row = tree_.row_of(boundary_node_);
    if (row) boundary_row_cache_ = *row;
    return row;
}

int TreeView::gap_above(std::size_t row) const noexcept {
    if (metrics_.group_gap == 0 || row < top_row_) return 0;
    const auto boundary = boundary_row();
    return boundary && *boundary >= top_row_ && row >= *boundary ? metrics_.group_gap : 0;
}

int TreeView::row_top(std::size_t row) const noexcept {
    return static_cast<int>(row - top_row_) * metrics_.row_height + gap_above(row);
}

std::size_t TreeView::max_top_row() const noexcept {
    const std::size_t rows = tree_.row_count();
    const auto visible = static_cast<std::size_t>(visible_rows());
    return rows > visible ? rows - visible : 0;
}

int TreeView::expander_left(std::uint16_t depth) const noexcept {
    return scale(4, metrics_.dpi) + depth * metrics_.indent;
}

int TreeView::content_left(std::uint16_t depth) const noexcept {
    return expander_left(depth) + metrics_.indent + metrics_.text_gap;
}

int TreeView::text_left(std::uint16_t depth) const noexcept {
    return content_left(depth) + metrics_.icon_width;
}

std::ptrdiff_t TreeView::row_at(int y) const noexcept {
    if (y < 0) return -1;
    if (metrics_.group_gap > 0) {
        if (const auto boundary = boundary_row(); boundary && *boundary >= top_row_) {
            const int gap_top = static_cast<int>(*boundary - top_row_) * metrics_.row_height;
            if (y >= gap_top + metrics_.group_gap) {
                y -= metrics_.group_gap;
            } else if (y >= gap_top) {
                return -1; // in the gap
            }
        }
    }
    const std::size_t row = top_row_ + static_cast<std::size_t>(y / metrics_.row_height);
    return row < tree_.row_count() ? static_cast<std::ptrdiff_t>(row) : -1;
}

void TreeView::invalidate_row(std::size_t row) noexcept {
    if (wnd_ == nullptr || row < top_row_) return;
    const std::size_t offset = row - top_row_;
    if (offset > static_cast<std::size_t>(visible_rows())) return;
    const int top = row_top(row);
    const RECT rect{0, top, client_width_, top + metrics_.row_height};
    InvalidateRect(wnd_, &rect, FALSE);
}

void TreeView::invalidate_from(std::size_t row) noexcept {
    if (wnd_ == nullptr) return;
    const std::size_t first = std::max(row, top_row_);
    const int top = static_cast<int>(first - top_row_) * metrics_.row_height; // gap included
    if (top >= client_height_) return;
    const RECT rect{0, top, client_width_, client_height_};
    InvalidateRect(wnd_, &rect, FALSE);
}

// --- Scrolling ------------------------------------------------------------------------------

void TreeView::update_scrollbar() noexcept {
    if (wnd_ == nullptr) return;
    SCROLLINFO info{sizeof(info)};
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (thin_scrollbar_) {
        // A page larger than the range hides the system bar; the thin thumb is drawn instead.
        info.nPage = 1;
        SetScrollInfo(wnd_, SB_VERT, &info, TRUE);
        invalidate_thin();
        return;
    }
    info.nMin = 0;
    info.nMax = static_cast<int>(std::min<std::size_t>(tree_.row_count(), 0x7ffffffe)) - 1;
    info.nPage = static_cast<UINT>(visible_rows());
    info.nPos = static_cast<int>(top_row_);
    // Showing or hiding the bar sends WM_SIZE re-entrantly; on_size copes (it only clamps).
    SetScrollInfo(wnd_, SB_VERT, &info, TRUE);
}

void TreeView::scroll_to(std::size_t top_row) noexcept {
    top_row = std::min(top_row, max_top_row());
    if (top_row == top_row_ || wnd_ == nullptr) return;
    end_rename(false); // the editor would no longer sit on its row
    update_tooltip(-1); // until the mouse moves onto a row again

    // The scroll moves pixels: paint the hovered row plain first, or its highlight moves along.
    if (hover_row_ >= 0) {
        const auto old_hover = static_cast<std::size_t>(hover_row_);
        hover_row_ = -1;
        invalidate_row(old_hover);
        UpdateWindow(wnd_);
    }
    const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(top_row_) -
                                 static_cast<std::ptrdiff_t>(top_row);
    // The gap moves with the rows only while it stays on screen; otherwise repaint everything.
    const auto boundary = metrics_.group_gap > 0 ? boundary_row() : std::nullopt;
    const bool gap_jumps = boundary && ((*boundary >= top_row_) != (*boundary >= top_row));
    top_row_ = top_row;
    // Transparent rows sit on a background that does not scroll: repaint them all.
    if (std::abs(delta) < visible_rows() && !gap_jumps && !transparent_) {
        // Move what is already on screen; only the uncovered strip gets painted.
        ScrollWindowEx(wnd_, 0, static_cast<int>(delta) * metrics_.row_height, nullptr, nullptr,
                       nullptr, nullptr, SW_INVALIDATE);
    } else {
        InvalidateRect(wnd_, nullptr, FALSE);
    }
    // Like Explorer: the row now under the mouse is the hovered one.
    POINT cursor{};
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) == wnd_ && ScreenToClient(wnd_, &cursor)) {
        hover_row_ = row_at(cursor.y);
        if (hover_row_ >= 0) invalidate_row(static_cast<std::size_t>(hover_row_));
    }

    if (thin_scrollbar_) {
        invalidate_thin(); // the thumb moved (and ScrollWindowEx moved its old picture)
        return;
    }
    SCROLLINFO info{sizeof(info)};
    info.fMask = SIF_POS;
    info.nPos = static_cast<int>(top_row_);
    SetScrollInfo(wnd_, SB_VERT, &info, TRUE);
}

void TreeView::ensure_visible(std::size_t row) noexcept {
    const auto visible = static_cast<std::size_t>(visible_rows());
    if (row < top_row_) {
        scroll_to(row);
    } else if (row >= top_row_ + visible) {
        scroll_to(row - visible + 1);
    }
}

void TreeView::on_vscroll(int code) noexcept {
    restore_top_node_ = model::no_node; // the user scrolls: stop holding a restored top row
    const auto page = static_cast<std::size_t>(std::max(visible_rows() - 1, 1));
    switch (code) {
    case SB_LINEUP: scroll_to(top_row_ > 0 ? top_row_ - 1 : 0); break;
    case SB_LINEDOWN: scroll_to(top_row_ + 1); break;
    case SB_PAGEUP: scroll_to(top_row_ > page ? top_row_ - page : 0); break;
    case SB_PAGEDOWN: scroll_to(top_row_ + page); break;
    case SB_TOP: scroll_to(0); break;
    case SB_BOTTOM: scroll_to(max_top_row()); break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
        // The 16-bit position in WPARAM overflows past 65535 rows; the track position does not.
        SCROLLINFO info{sizeof(info)};
        info.fMask = SIF_TRACKPOS;
        if (GetScrollInfo(wnd_, SB_VERT, &info)) {
            scroll_to(static_cast<std::size_t>(std::max(info.nTrackPos, 0)));
        }
        break;
    }
    default: break;
    }
}

void TreeView::on_wheel(int delta) noexcept {
    restore_top_node_ = model::no_node; // the user scrolls: stop holding a restored top row
    int lines = static_cast<int>(wheel_lines_);
    if (wheel_lines_ == WHEEL_PAGESCROLL) lines = std::max(visible_rows() - 1, 1);
    if (lines <= 0) return;

    // Accumulate, so high-resolution wheels (small deltas) still scroll.
    wheel_remainder_ += delta;
    const int rows = wheel_remainder_ * lines / WHEEL_DELTA;
    if (rows == 0) return;
    wheel_remainder_ -= rows * WHEEL_DELTA / lines;

    const auto target = static_cast<std::ptrdiff_t>(top_row_) - rows;
    scroll_to(static_cast<std::size_t>(std::max<std::ptrdiff_t>(target, 0)));
}

void TreeView::on_size() noexcept {
    if (wnd_ == nullptr) return;
    end_rename(false);
    RECT client{};
    GetClientRect(wnd_, &client);
    client_width_ = client.right - client.left;
    client_height_ = client.bottom - client.top;
    ensure_buffer(client_width_, client_height_);
    top_row_ = std::min(top_row_, max_top_row());
    update_scrollbar();
    // A width change moves every ellipsis, a height change exposes rows: repaint what is visible.
    InvalidateRect(wnd_, nullptr, FALSE);
}

void TreeView::on_dpi_changed() noexcept {
    remeasure();
    on_size();
}

// --- Selection and expansion ----------------------------------------------------------------

} // namespace filetree::view
