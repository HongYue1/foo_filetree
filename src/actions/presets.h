#pragma once

// The actions the Preferences drop-downs offer, in display order. Pure data (tested offline).
// Recursion is not part of a preset: the editor keeps each action's recursion as it was.

#include <cstddef>
#include <span>

#include "action.h"

namespace filetree::actions {

struct Preset {
    const wchar_t* label;
    Action action;
};

//! Presets for folders (includes Expand / collapse) or files (does not).
[[nodiscard]] std::span<const Preset> presets(bool folder) noexcept;

//! Index of the preset matching `action` (ignoring recursion), or 0 (None) if there is none.
[[nodiscard]] std::size_t preset_index(const Action& action, bool folder) noexcept;

//! The preset's action with `previous`'s recursion carried over.
[[nodiscard]] Action from_preset(std::size_t index, bool folder, const Action& previous) noexcept;

} // namespace filetree::actions
