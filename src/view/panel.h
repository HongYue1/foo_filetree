#pragma once

// What a host window contains: the address bar on top and the tree below it, each a child
// window. Hosts create one window, forward its messages here and feed colours and the font.
// Also keeps the panel's Back / Forward history (main thread, no timers).

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "../settings/settings_store.h"
#include "address_bar.h"
#include "filter_box.h"
#include "status_bar.h"
#include "theme.h"
#include "tree_view.h"

namespace filetree::view {

class Panel final : private settings::Listener {
public:
    struct HostHooks {
        //! foobar2000's keyboard shortcuts, for keys the tree does not use.
        std::function<bool(WPARAM key)> shortcut;
        //! Default UI layout editing: the context menu belongs to the host then.
        std::function<bool()> layout_editing;
    };

    Panel() = default;
    ~Panel() { detach(); }
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    //! Call from the host's WM_CREATE.
    void attach(HWND host, HostHooks hooks) noexcept;
    //! Call from the host's WM_DESTROY.
    void detach() noexcept;

    //! After attach(): what to show first, by the Startup setting. `saved` is this instance's
    //! stored state (empty for a new panel).
    void start(const settings::PanelState& saved) noexcept;
    void capture_state(settings::PanelState& out) const { tree_.capture_state(out); }
    void restore_state(const settings::PanelState& state) noexcept { tree_.restore_state(state); }

    void set_colours(const ViewColours& colours) noexcept;
    //! A font as the host reports it, at system DPI.
    void set_font(const LOGFONTW& font) noexcept;

    //! The host window's messages. False for what the host should pass to DefWindowProc.
    bool handle_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept;

    //! The host window covers nothing itself; its children paint everything.
    static constexpr DWORD window_styles = WS_CLIPCHILDREN;
    static constexpr UINT class_styles = 0;

private:
    static LRESULT CALLBACK tree_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT on_tree_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    //! Panel keys handled before the tree: Alt+Left/Right/Up, browser keys, Ctrl+L.
    bool on_panel_key(UINT msg, WPARAM key) noexcept;
    void on_settings_changed(std::uint32_t changes) noexcept override;
    void layout() noexcept;
    void on_selection() noexcept;
    void update_buttons() noexcept;
    void go_back() noexcept;
    void go_forward() noexcept;
    void go_to_history() noexcept;
    //! Puts the filter box where the setting wants it (bar / floating / hidden).
    void place_filter() noexcept;
    void open_filter() noexcept;
    void close_floating_filter() noexcept;
    //! After every tree paint: the status bar follows the selection and listings.
    void update_status() noexcept;

    HWND host_{};
    HWND tree_wnd_{};
    TreeView tree_;
    AddressBar address_;
    FilterBox filter_;
    StatusBar status_;
    bool show_status_{false};
    model::Summary status_summary_{}; //!< what status_ shows, to skip unchanged paints
    bool status_valid_{false};
    HostHooks hooks_;
    bool show_address_{true};
    settings::FilterBox filter_mode_{settings::FilterBox::bar};
    bool floating_open_{false}; //!< floating mode: the box is shown
    bool dark_{false};
    bool theme_applied_{false};

    // History: the selections the user stayed on. An entry is kept once the selection has rested
    // on it for history_dwell_ms; quicker moves (arrow keys) replace the newest entry instead.
    std::vector<std::wstring> history_;
    std::size_t history_at_{0};
    DWORD history_time_{0};
    std::vector<TreeView::Crumb> crumbs_; //!< reused
};

} // namespace filetree::view
