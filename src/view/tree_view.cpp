// TreeView: state, layout, scrolling, message dispatch and asynchronous loading. Painting is in
// tree_view_paint.cpp, keyboard/mouse/actions in tree_view_input.cpp.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <windowsx.h>

#include <algorithm>

#include "../fs/drives.h"
#include "../fs/fb2k_glue.h"
#include "../platform/dpi.h"

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif

namespace filetree::view {
namespace {

UINT window_dpi(HWND wnd) noexcept { return dpi::of_window(wnd); }
int system_dpi() noexcept { return dpi::system(); }
int scale(int dips, int dpi) noexcept { return MulDiv(dips, dpi, 96); }

} // namespace

TreeView::TreeView() = default;

TreeView::~TreeView() { detach(); }

void TreeView::attach(HWND wnd) noexcept {
    wnd_ = wnd;
    alive_ = std::make_shared<TreeView*>(this);
    settings::subscribe(this);
    refresh_options();
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
    settings::unsubscribe(this);
    restore_expand_.clear();
    restore_select_.clear();
    restore_top_.clear();
    end_rename(false);
    pending_select_ = {};
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
    pending_select_ = {};
    const settings::Settings& s = settings::current();
    // Favourites are not checked here (a network path could stall the UI); one that is gone
    // shows the load error when opened.
    const auto add_favourites = [&] {
        for (const std::wstring& path : s.favourites) {
            tree_.add_root(path, FILE_ATTRIBUTE_DIRECTORY, model::node_favourite);
        }
    };
    if (s.favourites_place == settings::FavouritesPlace::before) add_favourites();
    for (const std::wstring& root : fs::drive_roots()) {
        const wchar_t letter = root.empty() ? L'\0' : static_cast<wchar_t>(towupper(root[0]));
        if (letter >= L'A' && letter <= L'Z' && (s.hidden_drives & (1u << (letter - L'A'))) != 0) {
            continue;
        }
        tree_.add_root(root);
    }
    if (s.favourites_place == settings::FavouritesPlace::after) add_favourites();
    selected_row_ = hover_row_ = -1;
    top_row_ = 0;
    filter_hidden_selection_ = model::no_node;
    restore_top_node_ = model::no_node;
    if (tree_.filtered()) tree_.rebuild_rows();
    notify_selection();
}

void TreeView::apply_full_splice() noexcept {
    // Rows were rebuilt (name filter): find the selected and top nodes again.
    const auto relocate = [&](std::ptrdiff_t row) -> std::ptrdiff_t {
        if (row < 0) return -1;
        const std::uint32_t node = tree_.previous_node_at(static_cast<std::size_t>(row));
        if (node == model::no_node) return -1;
        const auto found = tree_.row_of(node);
        return found ? static_cast<std::ptrdiff_t>(*found) : -1;
    };
    const std::ptrdiff_t old_selected = selected_row_;
    const std::uint32_t old_node =
        old_selected >= 0 ? tree_.previous_node_at(static_cast<std::size_t>(old_selected))
                          : model::no_node;
    selected_row_ = relocate(selected_row_);
    if (selected_row_ < 0 && old_node != model::no_node) {
        filter_hidden_selection_ = old_node; // filtered out: select it again when it reappears
    } else if (selected_row_ < 0 && filter_hidden_selection_ != model::no_node) {
        if (const auto row = tree_.row_of(filter_hidden_selection_)) {
            selected_row_ = static_cast<std::ptrdiff_t>(*row);
            filter_hidden_selection_ = model::no_node;
        }
    }
    const std::ptrdiff_t top = relocate(static_cast<std::ptrdiff_t>(top_row_));
    top_row_ = top >= 0 ? static_cast<std::size_t>(top) : std::min(top_row_, max_top_row());
    top_row_ = std::min(top_row_, max_top_row());
    hover_row_ = -1;
    update_scrollbar();
    InvalidateRect(wnd_, nullptr, FALSE);
    if (selected_row_ >= 0) ensure_visible(static_cast<std::size_t>(selected_row_));
    const std::uint32_t new_node =
        selected_row_ >= 0 ? tree_.node_at_row(static_cast<std::size_t>(selected_row_))
                           : model::no_node;
    if (new_node != old_node) notify_selection();
}

void TreeView::set_filter(std::wstring_view text) noexcept {
    try {
        const model::RowSplice splice = tree_.set_filter(text);
        apply_splice(splice);
    } catch (...) {
    }
}

void TreeView::set_colours(const ViewColours& colours) noexcept {
    colours_ = colours;
    hover_background_ = blend(colours.background, colours.selection_background,
                              colours.dark ? 0.30 : 0.18);
    dim_text_ = blend(colours.text, colours.background, 0.45);
    expander_colour_ = blend(colours.text, colours.background, 0.35);
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
    end_rename(false); // the editor would no longer sit on its row

    // The scroll moves pixels: paint the hovered row plain first, or its highlight moves along.
    if (hover_row_ >= 0) {
        const auto old_hover = static_cast<std::size_t>(hover_row_);
        hover_row_ = -1;
        invalidate_row(old_hover);
        UpdateWindow(wnd_);
    }
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
    // Like Explorer: the row now under the mouse is the hovered one.
    POINT cursor{};
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) == wnd_ && ScreenToClient(wnd_, &cursor)) {
        hover_row_ = row_at(cursor.y);
        if (hover_row_ >= 0) invalidate_row(static_cast<std::size_t>(hover_row_));
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

void TreeView::select_row(std::size_t row) noexcept {
    if (row >= tree_.row_count()) return;
    if (selected_row_ >= 0) invalidate_row(static_cast<std::size_t>(selected_row_));
    const bool changed = selected_row_ != static_cast<std::ptrdiff_t>(row);
    filter_hidden_selection_ = model::no_node;
    selected_row_ = static_cast<std::ptrdiff_t>(row);
    invalidate_row(row);
    ensure_visible(row);
    if (changed) notify_selection();
}

void TreeView::apply_splice(const model::RowSplice& splice) noexcept {
    if (splice.empty()) return;
    end_rename(false);
    if (splice.full) {
        apply_full_splice();
        return;
    }
    bool moved_to_parent = false;
    const auto shift = [&](std::ptrdiff_t& row) {
        if (row < 0 || static_cast<std::size_t>(row) < splice.row) return;
        if (static_cast<std::size_t>(row) < splice.row + splice.removed) {
            row = static_cast<std::ptrdiff_t>(splice.row) - 1; // collapsed into the parent
            moved_to_parent = true;
        } else {
            row += static_cast<std::ptrdiff_t>(splice.inserted) -
                   static_cast<std::ptrdiff_t>(splice.removed);
        }
    };
    shift(selected_row_);
    if (moved_to_parent) notify_selection();
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
        try {
            request_check(node); // listed earlier: catch up with changes since, in the background
        } catch (...) {
        }
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
        refresh_options();
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
    std::erase_if(pending_,
                  [node](const PendingListing& p) { return p.node == node && !p.check; });
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
    apply_pending_select(node);
    if (!restore_expand_.empty() || !restore_select_.empty() || !restore_top_.empty()) {
        try {
            const model::Node& n = tree_.node(node);
            for (std::uint32_t child = n.first_child; n.has(model::node_loaded) &&
                                                      child < n.first_child + n.child_count;
                 ++child) {
                try_restore(child);
            }
        } catch (...) {
            restore_expand_.clear();
            restore_select_.clear();
            restore_top_.clear();
        }
    }
    apply_restore_top();
}

bool TreeView::handle_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept {
    result = 0;
    switch (msg) {
    case WM_CONTEXTMENU:
        on_context_menu(lp);
        return true;
    case WM_INITMENUPOPUP:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_MENUCHAR:
        return forward_menu_message(msg, wp, lp, result);
    case WM_CTLCOLOREDIT:
        return on_edit_colour(reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp), result);
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
    case WM_MBUTTONDOWN:
        on_middle_button(GET_Y_LPARAM(lp));
        return true;
    case WM_MOUSEMOVE:
        on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return true;
    case WM_MOUSELEAVE:
        on_mouse_leave();
        return true;
    case WM_KEYDOWN:
        return on_key(wp);
    case WM_CHAR:
        return on_char(static_cast<wchar_t>(wp), static_cast<DWORD>(GetMessageTime()));
    case WM_GETDLGCODE: {
        // DLGC_WANTARROWS covers the arrow keys only. Enter is a dialog key: without
        // DLGC_WANTMESSAGE for it, the host's dialog navigation eats it before WM_KEYDOWN.
        result = DLGC_WANTARROWS | DLGC_WANTCHARS;
        const auto* message = reinterpret_cast<const MSG*>(lp);
        if (message != nullptr && message->message == WM_KEYDOWN && message->wParam == VK_RETURN) {
            result |= DLGC_WANTMESSAGE;
        }
        return true;
    }
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


