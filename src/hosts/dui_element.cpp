// Default UI host. Owns a window and a TreeView; feeds the view colours, the font and messages.
// No drawing logic of its own.
//
// Idle cost: none. No timers; Default UI notifies colour, font and dark-mode changes.

#include <helpers/foobar2000+atl.h>

#include <helpers/BumpableElem.h>
#include <libPPUI/win32_op.h>

#include <uxtheme.h>

#include <algorithm>
#include <vector>

#include "../guids.h"
#include "../version.h"
#include "../view/tree_view.h"
#include "host_shared.h"

#pragma comment(lib, "uxtheme.lib")

namespace {

class FolderTreeElement;

// Live elements, so a Columns UI font change (which Default UI never forwards) reaches them.
std::vector<FolderTreeElement*>& live_elements() {
    static std::vector<FolderTreeElement*> elements;
    return elements;
}

class FolderTreeElement : public ui_element_instance, public CWindowImpl<FolderTreeElement> {
public:
    DECLARE_WND_CLASS_EX(TEXT("foo_filetree_dui_element"),
                         filetree::view::TreeView::class_styles, (-1));

    FolderTreeElement(ui_element_config::ptr config, ui_element_instance_callback_ptr callback)
        : config_(config), m_callback(callback) {
        live_elements().push_back(this);
    }

    ~FolderTreeElement() {
        auto& elements = live_elements();
        elements.erase(std::remove(elements.begin(), elements.end(), this), elements.end());
    }

    FolderTreeElement(const FolderTreeElement&) = delete;
    FolderTreeElement& operator=(const FolderTreeElement&) = delete;

    void initialize_window(HWND parent) {
        constexpr DWORD style = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
                                filetree::view::TreeView::window_styles;
        WIN32_OP(Create(parent, nullptr, nullptr, style) != NULL);
    }

    BEGIN_MSG_MAP_EX(FolderTreeElement)
        MSG_WM_CREATE(on_create)
        MSG_WM_DESTROY(on_destroy)
        MESSAGE_HANDLER_EX(WM_KEYDOWN, on_key_down)
        MESSAGE_HANDLER_EX(WM_SYSKEYDOWN, on_key_down)
        // Everything else the tree owns; what it declines falls through to DefWindowProc
        // (WM_CONTEXTMENU included, so Default UI's layout-edit menu still works).
        if (view_.handle_message(hWnd, uMsg, wParam, lParam, lResult)) return TRUE;
    END_MSG_MAP()

    HWND get_wnd() override { return *this; }

    // Nothing is stored yet (M6 adds per-instance state); keep whatever the host gave us so a
    // newer blob survives a round trip through this version.
    void set_configuration(ui_element_config::ptr config) override { config_ = config; }
    ui_element_config::ptr get_configuration() override { return config_; }

    static GUID g_get_guid() { return filetree::guids::dui_element; }
    static GUID g_get_subclass() { return ui_element_subclass_utility; }
    static void g_get_name(pfc::string_base& out) { out = FILETREE_NAME; }
    static ui_element_config::ptr g_get_default_configuration() {
        return ui_element_config::g_create_empty(g_get_guid());
    }
    static const char* g_get_description() {
        return "Browse folders on disk and send them to playlists.";
    }

    void notify(const GUID& what, t_size, const void*, t_size) override {
        // Default UI raises colors_changed for a dark-mode toggle too.
        if (what == ui_element_notify_colors_changed) apply_colours();
        if (what == ui_element_notify_font_changed) apply_font();
    }

    void refresh_font() noexcept {
        if (m_hWnd != nullptr) apply_font();
    }

private:
    int on_create(LPCREATESTRUCT) {
        view_.attach(*this);
        apply_colours();
        apply_font();
        return 0;
    }

    void on_destroy() { view_.detach(); }

    LRESULT on_key_down(UINT msg, WPARAM wp, LPARAM lp) {
        LRESULT result = 0;
        if (msg == WM_KEYDOWN && view_.handle_message(*this, msg, wp, lp, result)) return result;
        // Keys the tree does not use go to foobar2000's keyboard shortcuts.
        try {
            if (keyboard_shortcut_manager_v2::get()->process_keydown_simple(
                    static_cast<t_uint32>(wp))) {
                return 0;
            }
        } catch (...) {
        }
        SetMsgHandled(FALSE);
        return 0;
    }

    void apply_colours() {
        if (m_hWnd == nullptr) return;
        using filetree::view::blend;
        using filetree::view::more_contrast;

        filetree::view::ViewColours out;
        out.text = m_callback->query_std_color(ui_color_text);
        out.background = m_callback->query_std_color(ui_color_background);
        out.selection_background = m_callback->query_std_color(ui_color_selection);
        // Default UI has no selection-text colour: pick whichever of text/background reads better.
        out.selection_text = more_contrast(out.selection_background, out.text, out.background);
        out.inactive_selection_background = blend(out.background, out.selection_background, 0.5);
        out.inactive_selection_text =
            more_contrast(out.inactive_selection_background, out.text, out.background);
        out.dark = m_callback->is_dark_mode();

        if (out.dark != dark_ || !theme_applied_) {
            // Our scroll bar follows this window's theme.
            SetWindowTheme(*this, out.dark ? L"DarkMode_Explorer" : nullptr, nullptr);
            dark_ = out.dark;
            theme_applied_ = true;
        }
        view_.set_colours(out);
    }

    void apply_font() {
        // Columns UI's font first (see host_shared.h), then Default UI's list font.
        if (const auto cui = filetree::host::cui_font()) {
            view_.set_font(*cui);
        } else if (HFONT font = m_callback->query_font_ex(ui_font_lists); font != nullptr) {
            LOGFONTW logical{};
            if (GetObjectW(font, sizeof(logical), &logical) != 0) view_.set_font(logical);
        }
    }

    ui_element_config::ptr config_;
    filetree::view::TreeView view_;
    bool dark_{};
    bool theme_applied_{};

protected:
    // Must be protected for ui_element_impl_withpopup<>.
    const ui_element_instance_callback_ptr m_callback;
};

class FolderTreeElementImpl : public ui_element_impl_withpopup<FolderTreeElement> {};

static service_factory_single_t<FolderTreeElementImpl> g_folder_tree_element_factory;

} // namespace

namespace filetree::host {

void refresh_dui_elements() noexcept {
    for (FolderTreeElement* element : live_elements()) element->refresh_font();
}

} // namespace filetree::host
