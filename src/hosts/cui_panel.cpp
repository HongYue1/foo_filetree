// Columns UI host. Owns a window and a TreeView; feeds the view colours, the font and messages.
// No drawing logic of its own.
//
// Idle cost: none. No timers; Columns UI's colour/font notifications are the only callbacks.

#include <helpers/foobar2000+atl.h>

#include <columns_ui-sdk/ui_extension.h>

#include <uxtheme.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../guids.h"
#include "../version.h"
#include "../view/panel.h"
#include "host_shared.h"

#pragma comment(lib, "uxtheme.lib")

namespace {

class FolderTreePanel;

// The colour and font clients are single service instances with const callbacks, so a change is
// fanned out to the live panels by hand. Main thread only.
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

    // Per-instance state (settings/panel_state.h). Columns UI reads it while the window exists
    // too (saving the layout), so it comes from the live tree then.
    void set_config(stream_reader* reader, t_size size, abort_callback& abort) override {
        state_ = {};
        if (size == 0) return; // a new panel: defaults
        std::string bytes(size, '\0');
        reader->read_object(bytes.data(), size, abort);
        state_ = filetree::settings::PanelState::decode(bytes);
    }

    void get_config(stream_writer* writer, abort_callback& abort) const override {
        filetree::settings::PanelState state = state_;
        if (wnd_ != nullptr) {
            try {
                view_.capture_state(state);
            } catch (...) {
            }
        }
        const std::string bytes = state.encode();
        writer->write_object(bytes.data(), bytes.size(), abort);
    }

    // uie::window
    unsigned get_type() const override { return uie::type_panel; }
    void get_category(pfc::string_base& out) const override { out = "Panels"; }

    bool get_description(pfc::string_base& out) const override {
        out = "Browse folders on disk and send them to playlists.";
        return true;
    }

    // uie::container_uie_window_v3_t
    uie::container_window_v3_config get_window_config() override {
        // Opaque: the panel's children paint every pixel.
        uie::container_window_v3_config config(L"foo_filetree_cui_panel", false,
                                               filetree::view::Panel::class_styles);
        config.window_styles |= filetree::view::Panel::window_styles;
        return config;
    }

    // Columns UI calls this from layout code that cannot unwind, so nothing may escape.
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept override {
        try {
            switch (msg) {
            case WM_CREATE:
                wnd_ = wnd;
                view_.attach(wnd, filetree::view::Panel::HostHooks{
                                      [](WPARAM key) {
                                          return uie::window::g_process_keydown_keyboard_shortcuts(
                                              key);
                                      },
                                      {}});
                refresh_colours();
                refresh_font();
                view_.start(state_);
                return 0;

            case WM_DESTROY:
                try {
                    view_.capture_state(state_);
                } catch (...) {
                }
                view_.detach();
                wnd_ = nullptr;
                return 0;

            default: {
                LRESULT result = 0;
                if (view_.handle_message(wnd, msg, wp, lp, result)) return result;
                break;
            }
            }
        } catch (const std::exception& exception) {
            FB2K_console_formatter() << FILETREE_NAME << ": panel message failed: "
                                     << exception.what();
        } catch (...) {
            FB2K_console_formatter() << FILETREE_NAME << ": panel message failed";
        }

        return DefWindowProc(wnd, msg, wp, lp);
    }

    //! Re-read colours and dark mode. A no-op before the window exists.
    void refresh_colours() noexcept {
        if (wnd_ == nullptr) return;
        const cui::colours::helper colours(filetree::guids::cui_colour_client);
        filetree::view::ViewColours out;
        out.text = colours.get_colour(cui::colours::colour_text);
        out.background = colours.get_colour(cui::colours::colour_background);
        out.selection_text = colours.get_colour(cui::colours::colour_selection_text);
        out.selection_background = colours.get_colour(cui::colours::colour_selection_background);
        out.inactive_selection_text =
            colours.get_colour(cui::colours::colour_inactive_selection_text);
        out.inactive_selection_background =
            colours.get_colour(cui::colours::colour_inactive_selection_background);
        out.dark = colours.is_dark_mode_active();
        view_.set_colours(out);
    }

    void refresh_font() noexcept {
        if (wnd_ == nullptr) return;
        try {
            view_.set_font(cui::fonts::get_log_font_with_fallback(filetree::guids::cui_font_client));
        } catch (...) {
        }
    }

private:
    HWND wnd_{};
    filetree::view::Panel view_;
    filetree::settings::PanelState state_;
};

uie::window_factory<FolderTreePanel> g_folder_tree_panel_factory;

class FolderTreeColourClient : public cui::colours::client {
public:
    const GUID& get_client_guid() const override { return filetree::guids::cui_colour_client; }
    void get_name(pfc::string_base& out) const override { out = FILETREE_NAME; }

    uint32_t get_supported_colours() const override {
        return cui::colours::colour_flag_background | cui::colours::colour_flag_text |
               cui::colours::colour_flag_selection_text |
               cui::colours::colour_flag_selection_background |
               cui::colours::colour_flag_inactive_selection_text |
               cui::colours::colour_flag_inactive_selection_background;
    }

    uint32_t get_supported_bools() const override {
        return cui::colours::bool_flag_dark_mode_enabled;
    }

    // We draw with GDI ourselves, not with the Theme API.
    bool get_themes_supported() const override { return false; }

    void on_colour_changed(uint32_t) const override {
        for (FolderTreePanel* panel : live_panels()) panel->refresh_colours();
    }

    // The mask says what changed, not the new values; refresh_colours() re-reads them.
    void on_bool_changed(uint32_t changed_items_mask) const override {
        if ((changed_items_mask & cui::colours::bool_flag_dark_mode_enabled) != 0) {
            for (FolderTreePanel* panel : live_panels()) panel->refresh_colours();
        }
    }
};

cui::colours::client::factory<FolderTreeColourClient> g_folder_tree_colour_client;

class FolderTreeFontClient : public cui::fonts::client {
public:
    const GUID& get_client_guid() const override { return filetree::guids::cui_font_client; }
    void get_name(pfc::string_base& out) const override { out = FILETREE_NAME; }

    cui::fonts::font_type_t get_default_font_type() const override {
        return cui::fonts::font_type_items;
    }

    // Metrics change: the view re-measures rows in set_font().
    void on_font_changed() const override {
        for (FolderTreePanel* panel : live_panels()) panel->refresh_font();
        filetree::host::refresh_dui_elements();
    }
};

cui::fonts::client::factory<FolderTreeFontClient> g_folder_tree_font_client;

} // namespace

namespace filetree::host {

std::optional<LOGFONTW> cui_font() noexcept {
    try {
        if (auto font = cui::fonts::get_log_font(filetree::guids::cui_font_client)) {
            if (font->lfFaceName[0] != L'\0') return font;
        }
    } catch (...) {
    }
    return std::nullopt;
}

} // namespace filetree::host

