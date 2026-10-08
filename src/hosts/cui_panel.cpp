// Columns UI host. M0: an empty panel that paints the Columns UI background colour and follows
// dark mode. The tree view arrives in M2 and plugs in here; this file keeps no drawing logic
// beyond the background fill.
//
// Idle cost: none. No timers, no callbacks except Columns UI's own colour notifications, and
// WM_PAINT fills only the invalid rectangle with a cached brush.

#include <helpers/foobar2000+atl.h>

#include <columns_ui-sdk/ui_extension.h>

#include <uxtheme.h>

#include <algorithm>
#include <vector>

#include "../guids.h"
#include "../platform/gdi.h"
#include "../version.h"

#pragma comment(lib, "uxtheme.lib")

namespace {

class FolderTreePanel;

// The colour client is a single service instance with const callbacks, so a change notification
// is fanned out to the live panels by hand. Main thread only, which is where Columns UI raises
// these and where panels are created and destroyed.
std::vector<FolderTreePanel*>& live_panels() {
    static std::vector<FolderTreePanel*> panels;
    return panels;
}

class FolderTreePanel : public uie::container_uie_window_v3_t<> {
public:
    FolderTreePanel() { live_panels().push_back(this); }

    ~FolderTreePanel() {
        auto& panels = live_panels();
        panels.erase(std::remove(panels.begin(), panels.end(), this), panels.end());
    }

    FolderTreePanel(const FolderTreePanel&) = delete;
    FolderTreePanel& operator=(const FolderTreePanel&) = delete;

    // uie::extension_base
    const GUID& get_extension_guid() const override { return filetree::guids::cui_panel; }
    void get_name(pfc::string_base& out) const override { out = FILETREE_NAME; }

    // uie::window
    unsigned get_type() const override { return uie::type_panel; }
    void get_category(pfc::string_base& out) const override { out = "Panels"; }

    bool get_description(pfc::string_base& out) const override {
        out = "Browse folders on disk and send them to playlists.";
        return true;
    }

    // uie::container_uie_window_v3_t
    uie::container_window_v3_config get_window_config() override {
        // We paint our own background (opaque), so the parent's is not needed underneath.
        return {L"foo_filetree_cui_panel", false};
    }

    // Columns UI calls this from layout code that cannot unwind, so nothing may escape.
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept override {
        try {
            switch (msg) {
            case WM_CREATE:
                wnd_ = wnd;
                refresh();
                return 0;

            case WM_DESTROY:
                wnd_ = nullptr;
                background_.reset();
                return 0;

            case WM_ERASEBKGND:
                // WM_PAINT fills the invalid area; erasing first would only flicker.
                return TRUE;

            case WM_PAINT: {
                PAINTSTRUCT paint{};
                if (HDC dc = BeginPaint(wnd, &paint); dc != nullptr) {
                    filetree::gdi::fill_paint_rect(dc, paint.rcPaint, background_.get());
                    EndPaint(wnd, &paint);
                }
                return 0;
            }

            default:
                break;
            }
        } catch (const std::exception& exception) {
            FB2K_console_formatter() << FILETREE_NAME << ": panel message failed: "
                                     << exception.what();
        } catch (...) {
            FB2K_console_formatter() << FILETREE_NAME << ": panel message failed";
        }

        return DefWindowProc(wnd, msg, wp, lp);
    }

    //! Re-read colours and dark mode from Columns UI. A no-op before the window exists.
    void refresh() noexcept {
        if (wnd_ == nullptr) return;

        const cui::colours::helper colours(filetree::guids::cui_colour_client);
        const bool brush_changed =
            background_.set(colours.get_colour(cui::colours::colour_background));

        const bool dark = colours.is_dark_mode_active();
        if (dark != dark_ || !theme_applied_) {
            // Our scroll bars (M2) follow this window's theme.
            SetWindowTheme(wnd_, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
            dark_ = dark;
            theme_applied_ = true;
        }

        if (brush_changed) InvalidateRect(wnd_, nullptr, FALSE);
    }

private:
    HWND wnd_{};
    filetree::gdi::SolidBrush background_;
    bool dark_{};
    bool theme_applied_{};
};

void refresh_all_panels() noexcept {
    for (FolderTreePanel* panel : live_panels()) panel->refresh();
}

uie::window_factory<FolderTreePanel> g_folder_tree_panel_factory;

// Our entry on Columns UI's Colours page. Without it the panel would ignore the user's colours
// and never hear about a dark-mode switch. M2 adds the selection colours when they are drawn.
class FolderTreeColourClient : public cui::colours::client {
public:
    const GUID& get_client_guid() const override { return filetree::guids::cui_colour_client; }
    void get_name(pfc::string_base& out) const override { out = FILETREE_NAME; }

    uint32_t get_supported_colours() const override {
        return cui::colours::colour_flag_background | cui::colours::colour_flag_text;
    }

    uint32_t get_supported_bools() const override {
        return cui::colours::bool_flag_dark_mode_enabled;
    }

    // We draw with GDI ourselves, not with the Theme API.
    bool get_themes_supported() const override { return false; }

    void on_colour_changed(uint32_t) const override { refresh_all_panels(); }

    // The mask says what changed, not the new values; refresh() re-reads them.
    void on_bool_changed(uint32_t changed_items_mask) const override {
        if ((changed_items_mask & cui::colours::bool_flag_dark_mode_enabled) != 0) {
            refresh_all_panels();
        }
    }
};

cui::colours::client::factory<FolderTreeColourClient> g_folder_tree_colour_client;

} // namespace
