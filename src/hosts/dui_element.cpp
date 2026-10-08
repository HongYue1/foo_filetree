// Default UI host. M0: an empty element that paints the Default UI background colour and follows
// dark mode. The tree view arrives in M2 and plugs in here.
//
// Idle cost: none. No timers; Default UI notifies colour and dark-mode changes, and WM_PAINT fills
// only the invalid rectangle with a cached brush.

#include <helpers/foobar2000+atl.h>

#include <helpers/BumpableElem.h>
#include <libPPUI/win32_op.h>

#include <uxtheme.h>

#include "../guids.h"
#include "../platform/gdi.h"
#include "../version.h"

#pragma comment(lib, "uxtheme.lib")

namespace {

class FolderTreeElement : public ui_element_instance, public CWindowImpl<FolderTreeElement> {
public:
    DECLARE_WND_CLASS_EX(TEXT("foo_filetree_dui_element"), 0, (-1));

    FolderTreeElement(ui_element_config::ptr config, ui_element_instance_callback_ptr callback)
        : config_(config), m_callback(callback) {}

    FolderTreeElement(const FolderTreeElement&) = delete;
    FolderTreeElement& operator=(const FolderTreeElement&) = delete;

    void initialize_window(HWND parent) { WIN32_OP(Create(parent) != NULL); }

    BEGIN_MSG_MAP_EX(FolderTreeElement)
        MSG_WM_CREATE(on_create)
        MSG_WM_DESTROY(on_destroy)
        MSG_WM_ERASEBKGND(on_erase_background)
        MSG_WM_PAINT(on_paint)
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
        if (what == ui_element_notify_colors_changed) apply_theme();
    }

private:
    int on_create(LPCREATESTRUCT) {
        apply_theme();
        return 0;
    }

    void on_destroy() { background_.reset(); }

    // WM_PAINT fills the invalid area; erasing first would only flicker.
    BOOL on_erase_background(CDCHandle) { return TRUE; }

    void on_paint(CDCHandle) {
        PAINTSTRUCT paint{};
        if (HDC dc = BeginPaint(&paint); dc != nullptr) {
            filetree::gdi::fill_paint_rect(dc, paint.rcPaint, background_.get());
            EndPaint(&paint);
        }
    }

    void apply_theme() {
        if (m_hWnd == nullptr) return;

        const bool brush_changed =
            background_.set(m_callback->query_std_color(ui_color_background));

        const bool dark = m_callback->is_dark_mode();
        if (dark != dark_ || !theme_applied_) {
            // Our scroll bars (M2) follow this window's theme.
            SetWindowTheme(*this, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
            dark_ = dark;
            theme_applied_ = true;
        }

        if (brush_changed) Invalidate(FALSE);
    }

    ui_element_config::ptr config_;
    filetree::gdi::SolidBrush background_;
    bool dark_{};
    bool theme_applied_{};

protected:
    // Must be protected for ui_element_impl_withpopup<>.
    const ui_element_instance_callback_ptr m_callback;
};

class FolderTreeElementImpl : public ui_element_impl_withpopup<FolderTreeElement> {};

static service_factory_single_t<FolderTreeElementImpl> g_folder_tree_element_factory;

} // namespace
