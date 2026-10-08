#pragma once

// The live settings: loaded from cfg vars on first use, saved by apply(), broadcast to live
// views. Main thread only.

#include <cstdint>
#include <memory>

#include "../model/filter_rules.h"
#include "settings_model.h"

namespace filetree::settings {

class Listener {
public:
    //! `changes` is a mask of settings::Change.
    virtual void on_settings_changed(std::uint32_t changes) noexcept = 0;

protected:
    ~Listener() = default;
};

//! What panels use: the saved settings, or a Preferences preview while one is active.
[[nodiscard]] const Settings& current();
//! The saved settings, ignoring any preview (what the Preferences page compares against).
[[nodiscard]] const Settings& stored();

//! Sanitises, saves, ends any preview and notifies every listener of what changed.
void apply(Settings next);

//! Shows `next` in every panel without saving it (live preview while editing Preferences).
void preview(Settings next);
//! Drops the preview: panels go back to the saved settings. No-op without a preview.
void end_preview();

//! The filter rules for `current()`, rebuilt only when they change. Shared with workers.
[[nodiscard]] std::shared_ptr<const model::FilterRules> filter_rules();

void subscribe(Listener* listener);
void unsubscribe(Listener* listener) noexcept;

} // namespace filetree::settings
