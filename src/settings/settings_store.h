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

[[nodiscard]] const Settings& current();

//! Sanitises, saves and notifies every listener of what changed. No-op if nothing did.
void apply(Settings next);

//! The filter rules for `current()`, rebuilt only when they change. Shared with workers.
[[nodiscard]] std::shared_ptr<const model::FilterRules> filter_rules();

void subscribe(Listener* listener);
void unsubscribe(Listener* listener) noexcept;

} // namespace filetree::settings
