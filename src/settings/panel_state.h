#pragma once

// One panel's own state (per instance, stored by the host: Default UI element config, Columns UI
// panel config): what was open, selected and at the top. Plain data and a text codec, no fb2k,
// so it is unit-tested offline.

#include <string>
#include <string_view>
#include <vector>

namespace filetree::settings {

struct PanelState {
    std::vector<std::wstring> expanded; //!< folders that were open, parents before children
    std::wstring selected;
    std::wstring top; //!< the first visible row

    [[nodiscard]] bool empty() const noexcept {
        return expanded.empty() && selected.empty() && top.empty();
    }

    //! UTF-8 text: a "foo_filetree state 1" line, then one "E|S|T <path>" line per entry.
    //! Readable in a layout dump and easy to extend: readers skip lines they do not know.
    [[nodiscard]] std::string encode() const;
    //! Never fails: anything without the header (an empty blob, another format) gives an empty
    //! state; unknown or malformed lines are skipped.
    [[nodiscard]] static PanelState decode(std::string_view bytes);

    friend bool operator==(const PanelState&, const PanelState&) = default;
};

} // namespace filetree::settings
