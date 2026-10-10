// TreeView: state, message dispatch and asynchronous loading. Layout and scrolling are in
// tree_view_layout.cpp, painting in tree_view_paint.cpp, keyboard/mouse in tree_view_input.cpp.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <windowsx.h>

#include <algorithm>

#include "../fs/drives.h"
#include "../fs/fb2k_glue.h"
#include "../fs/library_folders.h"
#include "../platform/dpi.h"
#include "icon_font.h"

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif

namespace filetree::view {
namespace {

// Live views for the main-menu commands, and the one focused last.
std::vector<TreeView*> g_views;
TreeView* g_active_view = nullptr;

void library_changed() {
    const std::vector<TreeView*> views = g_views;
    for (TreeView* view : views) {
        if (std::find(g_views.begin(), g_views.end(), view) != g_views.end()) {
            view->on_library_changed();
        }
    }
}

void register_view(TreeView* view) {
    try {
        fs::set_library_listener(&library_changed);
        g_views.push_back(view);
    } catch (...) {
    }
}

void unregister_view(TreeView* view) noexcept {
    std::erase(g_views, view);
    if (g_active_view == view) g_active_view = nullptr;
}

void set_active_view(TreeView* view) noexcept { g_active_view = view; }

} // namespace

TreeView::TreeView() = default;

TreeView* TreeView::active() noexcept {
    if (g_active_view != nullptr) return g_active_view;
    return g_views.empty() ? nullptr : g_views.front();
}

TreeView::~TreeView() { detach(); }

void TreeView::attach(HWND wnd) noexcept {
    wnd_ = wnd;
    alive_ = std::make_shared<TreeView*>(this);
    settings::subscribe(this);
    refresh_options();
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheel_lines_, 0);
    remeasure();
    HRESULT drop_hr = S_OK;
    drop_target_ = DropTarget::attach(wnd, *this, drop_hr);
    register_view(this);
    now_playing::subscribe(this);
    if (drop_target_ == nullptr) {
        FB2K_console_formatter() << "Folder Tree: dropping onto the panel is unavailable "
                                    "(RegisterDragDrop "
                                 << pfc::format_hex(static_cast<std::uint32_t>(drop_hr), 8) << ")";
    }
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
    stop_watching();
    set_cut({});
    now_playing::unsubscribe(this);
    unregister_view(this);
    playing_count_ = 0;
    if (drop_target_ != nullptr) {
        drop_target_->detach();
        drop_target_ = nullptr;
    }
    drop_row_ = -1;
    ++generation_;
    alive_.reset();
    release_buffer();
    release_accessible();
    tooltip_.destroy();
    tip_row_ = -1;
    if (icon_font_ != nullptr) {
        DeleteObject(icon_font_);
        icon_font_ = nullptr;
    }
    if (mark_font_ != nullptr) {
        DeleteObject(mark_font_);
        mark_font_ = nullptr;
    }
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
    set_cut({});
    tree_.clear();
    pending_select_ = {};
    const settings::Settings& s = settings::current();
    // Favourites are not checked here (a network path could stall the UI); one that is gone
    // shows the load error when opened.
    const auto add_favourites = [&] {
        for (const std::wstring& path : s.favourites) {
            tree_.add_root(path, FILE_ATTRIBUTE_DIRECTORY, model::node_favourite);
        }
        // Then the library folders that are not favourites already.
        library_ = s.library_roots ? fs::library_index() : nullptr;
        if (library_ == nullptr) return;
        for (const std::wstring& root : library_->roots()) {
            const bool listed = std::any_of(
                s.favourites.begin(), s.favourites.end(), [&](const std::wstring& favourite) {
                    return CompareStringOrdinal(favourite.c_str(), static_cast<int>(favourite.size()),
                                                root.c_str(), static_cast<int>(root.size()),
                                                TRUE) == CSTR_EQUAL;
                });
            if (!listed) {
                tree_.add_root(root, FILE_ATTRIBUTE_DIRECTORY,
                               static_cast<std::uint16_t>(model::node_favourite | model::node_library));
            }
        }
    };
    if (s.favourites_place == settings::FavouritesPlace::before) add_favourites();
    if (s.show_drives) {
        for (const std::wstring& root : fs::drive_roots()) {
            const wchar_t letter = root.empty() ? L'\0' : static_cast<wchar_t>(towupper(root[0]));
            const bool hidden = letter >= L'A' && letter <= L'Z' &&
                                (s.hidden_drives & (1u << (letter - L'A'))) != 0;
            if (hidden) continue;
            tree_.add_root(root);
        }
    }
    if (s.favourites_place == settings::FavouritesPlace::after) add_favourites();
    boundary_node_ = model::no_node;
    boundary_row_cache_ = 0;
    for (std::uint32_t root = 1;
         root < tree_.node_count() && tree_.node(root).has(model::node_root); ++root) {
        if (tree_.node(root).has(model::node_favourite) != tree_.node(0).has(model::node_favourite)) {
            boundary_node_ = root;
            break;
        }
    }
    selected_row_ = hover_row_ = -1;
    top_row_ = 0;
    filter_hidden_selection_ = model::no_node;
    restore_top_node_ = model::no_node;
    if (tree_.filtered()) tree_.rebuild_rows();
    resolve_playing();
    notify_selection();
}

void TreeView::apply_full_splice() noexcept {
    schedule_watch_sync();
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

void TreeView::apply_splice(const model::RowSplice& splice) noexcept {
    if (splice.empty()) return;
    schedule_watch_sync();
    acc_event(EVENT_OBJECT_REORDER, -1); // child ids are rows
    if (splice.full) {
        apply_full_splice();
        follow_rename();
        resolve_playing();
        return;
    }
    // The folder above the change opened or closed.
    if (splice.row > 0) acc_event(EVENT_OBJECT_STATECHANGE, static_cast<std::ptrdiff_t>(splice.row) - 1);
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
    if (moved_to_parent) {
        // The focus was inside a collapsed folder: the folder takes it, selected if nothing
        // else still is.
        if (selected_row_ >= 0 && tree_.count_selected_rows(1) == 0) {
            const std::uint32_t parent = tree_.node_at_row(static_cast<std::size_t>(selected_row_));
            try {
                tree_.set_selected(parent, true);
            } catch (...) {
            }
            tree_.set_anchor(parent);
        }
        notify_selection();
    }
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
    follow_rename(); // a listing landing elsewhere must not end a rename
    resolve_playing();

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
    bool again = false;
    std::erase_if(pending_, [node, &again](const PendingListing& p) {
        if (p.node != node || p.check) return false;
        again = again || p.again;
        return true;
    });
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
    try {
        if (again) {
            // A change was reported while this listing ran: check once more now it has landed.
            request_check(node);
        } else {
            request_probe(node, listing);
        }
    } catch (...) {
    }
}

bool TreeView::handle_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept {
    result = 0;
    switch (msg) {
    case WM_NOTIFY:
        return lp != 0 && on_tooltip_notify(*reinterpret_cast<const NMHDR*>(lp), result);
    case WM_CLIPBOARDUPDATE:
        on_clipboard_update();
        return false; // others may listen too
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
        if (focused_) set_active_view(this);
        if (focused_) acc_focus_changed();
        if (tree_.selection_hint() > 1) {
            InvalidateRect(wnd_, nullptr, FALSE); // every selected row changes colour
        } else if (selected_row_ >= 0) {
            invalidate_row(static_cast<std::size_t>(selected_row_));
        }
        return true;
    case WM_TIMER:
        return on_watch_timer(static_cast<UINT_PTR>(wp));
    case WM_GETOBJECT:
        return on_get_object(wp, lp, result);
    case watch_message:
        on_watch_notify(static_cast<fs::WatchId>(lp));
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


