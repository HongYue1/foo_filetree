#pragma once

// User filter rules applied while enumerating (on workers): extensions that are always or never
// shown, and name patterns that hide files and folders. Built once per settings change on the
// main thread, then shared read-only as shared_ptr<const FilterRules>. Matching never allocates.

#include <string>
#include <string_view>
#include <vector>

#include "extension_set.h"

namespace filetree::model {

//! Whole-name wildcard match: '*' any run (also empty), '?' one character. Both arguments must
//! already be upper-cased with to_upper(). Iterative, no recursion, no allocation.
[[nodiscard]] bool glob_match(std::wstring_view pattern, std::wstring_view name) noexcept;

//! Invariant-culture upper case of up to `capacity` characters into `out`; returns the length.
//! Longer input is truncated (names are at most 255 characters).
std::size_t to_upper(std::wstring_view text, wchar_t* out, std::size_t capacity) noexcept;

struct FilterRules {
    ExtensionSet always_show; //!< shown even when only playable files are listed
    ExtensionSet never_show;  //!< never shown, whatever the file mode
    std::vector<std::wstring> hide_patterns; //!< upper-cased globs

    //! Replaces the patterns from a user list ("@eaDir; *.tmp | Thumbs.db"; also ',' and new
    //! lines). Blank entries are dropped.
    void set_hide_patterns(std::wstring_view list);

    [[nodiscard]] bool hidden_by_pattern(std::wstring_view name) const noexcept;

    [[nodiscard]] bool empty() const noexcept {
        return always_show.empty() && never_show.empty() && hide_patterns.empty();
    }
};

} // namespace filetree::model
