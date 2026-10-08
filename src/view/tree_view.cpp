// TreeView: state, layout, scrolling, input and asynchronous loading. Painting is in
// tree_view_paint.cpp.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <windowsx.h>

#include <algorithm>

#include "../fs/drives.h"
#include "../fs/fb2k_glue.h"

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif

namespace filetree::view {
namespace {

// GetDpiForWindow is Windows 10 1607+. foobar2000 v2 still runs on Windows 7, where a static
// import would stop the DLL from loading at all, so it is looked up once at run time.
UINT window_dpi(HWND wnd) noexcept {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto get_dpi_for_window = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (get_dpi_for_window != nullptr && wnd != nullptr) {
        if (const UINT dpi = get_dpi_for_window(wnd); dpi != 0) return dpi;
    }
    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}

//! The DPI host fonts are reported at (system DPI for a per-monitor-aware process).
int system_dpi() noexcept {
    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    return dpi > 0 ? dpi : 96;
}

int scale(int dips, int dpi) noexcept { return MulDiv(dips, dpi, 96); }

} // namespace

TreeView::TreeView() = default;

TreeView::~TreeView() { detach(); }

void TreeView::attach(HWND wnd) noexcept {
    wnd_ = wnd;
    alive_ = std::make_shared<TreeView*>(this);
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheel_lines_, 0);
    remeasure();
    try {
        populate_roots();
    } catch (...) {
        tree_.clear();
    }
    on_size();
}

void TreeView::detach() noexcept {
    for (const PendingListing& pending : pending_) pending.ticket.cancel();
    pending_.clear();
    ++generation_;
    alive_.reset();
    release_buffer();
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    tree_.clear();
    selected_row_ = hover_row_ = -1;
    top_row_ = 0;
    wnd_ = nullptr;
}

void TreeView::populate_roots() {
    for (const PendingListing& pending : pending_) pending.ticket.cancel();
    pending_.clear();
    ++generation_;
    tree_.clear();
    for (const std::wstring& root : fs::drive_roots()) tree_.add_root(root);
    selected_row_ = hover_row_ = -1;
    top_row_ = 0;
}

void TreeView::set_colours(const ViewColours& colours) noexcept {
    colours_ = colours;
    hover_background_ = blend(colours.background, colours.selection_background,
                              colours.dark ? 0.30 : 0.18);
    dim_text_ = blend(colours.text, colours.background, 0.45);
    expander_colour_ = blend(colours.text, colours.background, 0.35);
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
    metrics_.row_height = std::max<int>(tm.tmHeight + 2 * scale(3, dpi), 1);
    metrics_.indent = scale(16, dpi);
    metrics_.expander = std::max(scale(8, dpi) | 1, 5); // odd, so the glyph has a centre pixel
    metrics_.text_gap = scale(2, dpi);
    metrics_.text_ascent = tm.tmAscent;
}

// --- Geometry -------------------------------------------------------------------------------

int TreeView::visible_rows() const noexcept {
    return std::max(client_height_ / metrics_.row_height, 1);
}

std::size_t TreeView::max_top_row() const noexcept {
    const std::size_t rows = tree_.row_count();
    const auto visible = static_cast<std::size_t>(visible_rows());
    return rows > visible ? rows - visible : 0;
}

int TreeView::expander_left(std::uint16_t depth) const noexcept {
    return scale(4, metrics_.dpi) + depth * metrics_.indent;
}

int TreeView::text_left(std::uint16_t depth) const noexcept {
    return expander_left(depth) + metrics_.indent + metrics_.text_gap;
}

std::ptrdiff_t TreeView::row_at(int y) const noexcept {
    if (y < 0) return -1;
    const std::size_t row = top_row_ + static_cast<std::size_t>(y / metrics_.row_height);
    return row < tree_.row_count() ? static_cast<std::ptrdiff_t>(row) : -1;
}

void TreeView::invalidate_row(std::size_t row) noexcept {
    if (wnd_ == nullptr || row < top_row_) return;
    const std::size_t offset = row - top_row_;
    if (offset > static_cast<std::size_t>(visible_rows())) return;
    const int top = static_cast<int>(offset) * metrics_.row_height;
    const RECT rect{0, top, client_width_, top + metrics_.row_height};
    InvalidateRect(wnd_, &rect, FALSE);
}

void TreeView::invalidate_from(std::size_t row) noexcept {
    if (wnd_ == nullptr) return;
    const std::size_t first = std::max(row, top_row_);
    const int top = static_cast<int>(first - top_row_) * metrics_.row_height;
    if (top >= client_height_) return;
    const RECT rect{0, top, client_width_, client_height_};
    InvalidateRect(wnd_, &rect, FALSE);
}

// --- Scrolling ------------------------------------------------------------------------------

void TreeView::update_scrollbar() noexcept {
    if (wnd_ == nullptr) return;
    SCROLLINFO info{sizeof(info)};
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
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

    const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(top_row_) -
                                 static_cast<std::ptrdiff_t>(top_row);
    top_row_ = top_row;
    if (std::abs(delta) < visible_rows()) {
        // Move what is already on screen; only the uncovered strip gets painted.
        ScrollWindowEx(wnd_, 0, static_cast<int>(delta) * metrics_.row_height, nullptr, nullptr,
                       nullptr, nullptr, SW_INVALIDATE);
    } else {
        InvalidateRect(wnd_, nullptr, FALSE);
    }
    hover_row_ = -1; // re-evaluated on the next mouse move

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

void TreeView::select_row(std::size_t row) noexcept {
    if (row >= tree_.row_count()) return;
    if (selected_row_ >= 0) invalidate_row(static_cast<std::size_t>(selected_row_));
    selected_row_ = static_cast<std::ptrdiff_t>(row);
    invalidate_row(row);
    ensure_visible(row);
}

void TreeView::apply_splice(const model::RowSplice& splice) noexcept {
    if (splice.empty()) return;
    const auto shift = [&](std::ptrdiff_t& row) {
        if (row < 0 || static_cast<std::size_t>(row) < splice.row) return;
        if (static_cast<std::size_t>(row) < splice.row + splice.removed) {
            row = static_cast<std::ptrdiff_t>(splice.row) - 1; // collapsed into the parent
        } else {
            row += static_cast<std::ptrdiff_t>(splice.inserted) -
                   static_cast<std::ptrdiff_t>(splice.removed);
        }
    };
    shift(selected_row_);
    hover_row_ = -1;

    // Rows inserted or removed above the viewport must not move what the user is looking at.
    const std::size_t old_top = top_row_;
    if (splice.row < top_row_) {
        if (top_row_ < splice.row + splice.removed) {
            top_row_ = splice.row;
        } else {
            top_row_ = top_row_ + splice.inserted - splice.removed;
        }
    }
    top_row_ = std::min(top_row_, max_top_row());
    update_scrollbar();
    if (top_row_ != old_top) {
        InvalidateRect(wnd_, nullptr, FALSE);
    } else {
        invalidate_from(splice.row > 0 ? splice.row - 1 : 0);
    }

    // Like Explorer: when the expanded folder is the selected one, scroll so its new children
    // show, without pushing the folder itself off the top.
    if (splice.inserted > 0 && selected_row_ == static_cast<std::ptrdiff_t>(splice.row) - 1) {
        const std::size_t parent = splice.row - 1;
        const std::size_t last = splice.row + splice.inserted - 1;
        const auto visible = static_cast<std::size_t>(visible_rows());
        if (last >= top_row_ + visible) {
            scroll_to(std::min(parent, last - visible + 1));
        }
    }
}

void TreeView::toggle(std::uint32_t node) noexcept {
    const model::Node& n = tree_.node(node);
    if (n.has(model::node_expanded) && !n.has(model::node_load_failed)) {
        collapse(node);
    } else {
        expand(node);
    }
}

void TreeView::expand(std::uint32_t node) noexcept {
    model::RowSplice splice;
    switch (tree_.expand(node, &splice)) {
    case model::Tree::ExpandResult::expanded:
        apply_splice(splice);
        break;
    case model::Tree::ExpandResult::needs_load:
        try {
            request_listing(node);
        } catch (...) {
            tree_.fail_load(node);
        }
        [[fallthrough]];
    case model::Tree::ExpandResult::loading:
        if (const auto row = tree_.row_of(node)) invalidate_row(*row);
        break;
    default:
        break;
    }
}

void TreeView::collapse(std::uint32_t node) noexcept {
    const model::RowSplice splice = tree_.collapse(node);
    if (!splice.empty()) {
        apply_splice(splice);
    } else if (const auto row = tree_.row_of(node)) {
        invalidate_row(*row);
    }
}

void TreeView::request_listing(std::uint32_t node) {
    if (options_.files == fs::FileMode::playable && options_.playable == nullptr) {
        options_.playable = fs::playable_extensions();
    }
    tree_.build_path(node, path_);
    std::weak_ptr<TreeView*> weak = alive_;
    const std::uint64_t generation = generation_;
    auto ticket = fs::enumeration().request(
        path_, options_, [weak, node, generation](fs::Listing& listing) {
            if (const auto alive = weak.lock()) (*alive)->on_listing(node, generation, listing);
        });
    pending_.push_back({node, std::move(ticket)});
}

void TreeView::on_listing(std::uint32_t node, std::uint64_t generation,
                          fs::Listing& listing) noexcept {
    std::erase_if(pending_, [node](const PendingListing& p) { return p.node == node; });
    if (generation != generation_ || wnd_ == nullptr || node >= tree_.node_count()) return;

    model::RowSplice splice;
    if (listing.error != ERROR_SUCCESS) {
        tree_.fail_load(node);
    } else {
        try {
            listing.to_records(records_);
            splice = tree_.apply_children(node, records_);
        } catch (...) {
            tree_.fail_load(node);
        }
    }
    if (!splice.empty()) {
        apply_splice(splice);
    } else if (const auto row = tree_.row_of(node)) {
        invalidate_row(*row); // empty folder or error: the expander changes
    }
}

// --- Input ----------------------------------------------------------------------------------

bool TreeView::on_key(WPARAM key) noexcept {
    const std::size_t rows = tree_.row_count();
    if (rows == 0) return false;
    const bool has_selection = selected_row_ >= 0;
    const std::size_t current = has_selection ? static_cast<std::size_t>(selected_row_) : top_row_;
    const auto page = static_cast<std::size_t>(std::max(visible_rows() - 1, 1));
    const std::uint32_t node = tree_.node_at_row(current);
    const model::Node& n = tree_.node(node);

    switch (key) {
    case VK_UP: select_row(has_selection && current > 0 ? current - 1 : current); return true;
    case VK_DOWN: select_row(has_selection ? std::min(current + 1, rows - 1) : current); return true;
    case VK_PRIOR: select_row(current > page ? current - page : 0); return true;
    case VK_NEXT: select_row(std::min(current + page, rows - 1)); return true;
    case VK_HOME: select_row(0); return true;
    case VK_END: select_row(rows - 1); return true;
    case VK_LEFT:
        if (!has_selection) {
            select_row(current);
        } else if (n.has(model::node_expanded)) {
            collapse(node);
        } else if (n.parent != model::no_node) {
            if (const auto parent = tree_.row_of(n.parent)) select_row(*parent);
        }
        return true;
    case VK_RIGHT:
        if (!has_selection) {
            select_row(current);
        } else if (n.has(model::node_container)) {
            if (!n.has(model::node_expanded) || n.has(model::node_load_failed)) {
                expand(node);
            } else if (current + 1 < rows &&
                       tree_.node(tree_.node_at_row(current + 1)).parent == node) {
                select_row(current + 1);
            }
        }
        return true;
    case VK_ADD:
        if (has_selection) expand(node);
        return true;
    case VK_SUBTRACT:
        if (has_selection) collapse(node);
        return true;
    case VK_RETURN:
        // M3 turns Enter into a playlist action; until then it toggles folders.
        if (has_selection && n.has(model::node_container)) toggle(node);
        return true;
    default:
        return false;
    }
}

void TreeView::on_button_down(int x, int y, bool double_click) noexcept {
    if (wnd_ != nullptr && GetFocus() != wnd_) SetFocus(wnd_);
    const std::ptrdiff_t row = row_at(y);
    if (row < 0) return;
    const std::uint32_t node = tree_.node_at_row(static_cast<std::size_t>(row));
    const model::Node& n = tree_.node(node);

    const int expander_x = expander_left(n.depth);
    const bool on_expander = n.has(model::node_container) && x >= expander_x &&
                             x < expander_x + metrics_.indent;
    if (on_expander) {
        toggle(node);
        return;
    }
    select_row(static_cast<std::size_t>(row));
    if (double_click && n.has(model::node_container)) toggle(node);
}

void TreeView::on_mouse_move(int, int y) noexcept {
    if (!tracking_mouse_ && wnd_ != nullptr) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, wnd_, 0};
        tracking_mouse_ = TrackMouseEvent(&track) != FALSE;
    }
    const std::ptrdiff_t row = row_at(y);
    if (row == hover_row_) return;
    if (hover_row_ >= 0) invalidate_row(static_cast<std::size_t>(hover_row_));
    hover_row_ = row;
    if (row >= 0) invalidate_row(static_cast<std::size_t>(row));
}

void TreeView::on_mouse_leave() noexcept {
    tracking_mouse_ = false;
    if (hover_row_ >= 0) invalidate_row(static_cast<std::size_t>(hover_row_));
    hover_row_ = -1;
}

bool TreeView::handle_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept {
    result = 0;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        if (HDC dc = BeginPaint(wnd, &ps); dc != nullptr) {
            paint(dc, ps.rcPaint);
            EndPaint(wnd, &ps);
        }
        return true;
    }
    case WM_ERASEBKGND:
        result = 1; // paint covers every pixel
        return true;
    case WM_SIZE:
        on_size();
        return true;
    case WM_VSCROLL:
        on_vscroll(LOWORD(wp));
        return true;
    case WM_MOUSEWHEEL:
        on_wheel(GET_WHEEL_DELTA_WPARAM(wp));
        return true;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        on_button_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), msg == WM_LBUTTONDBLCLK);
        return true;
    case WM_MOUSEMOVE:
        on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return true;
    case WM_MOUSELEAVE:
        on_mouse_leave();
        return true;
    case WM_KEYDOWN:
        return on_key(wp);
    case WM_GETDLGCODE:
        result = DLGC_WANTARROWS | DLGC_WANTCHARS;
        return true;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        focused_ = msg == WM_SETFOCUS;
        if (selected_row_ >= 0) invalidate_row(static_cast<std::size_t>(selected_row_));
        return true;
    case WM_DPICHANGED_AFTERPARENT:
        on_dpi_changed();
        return true;
    case WM_SETTINGCHANGE:
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheel_lines_, 0);
        return false;
    default:
        return false;
    }
}

} // namespace filetree::view
