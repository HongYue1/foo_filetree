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
    //! Folders hidden by full path: environment variables expanded, upper-cased globs
    //! ("C:\WINDOWS", "?:\$RECYCLE.BIN").
    std::vector<std::wstring> hide_paths;

    //! Replaces the patterns from a user list ("@eaDir; *.tmp | Thumbs.db"; also ',' and new
    //! lines). Blank entries are dropped.
    void set_hide_patterns(std::wstring_view list);

    //! Replaces the hidden folders from user entries ("%WINDIR%", "?:\$Recycle.Bin"). An entry
    //! whose variable is not set is dropped. Main thread (reads the environment).
    void set_hide_paths(const std::vector<std::wstring>& paths);

    [[nodiscard]] bool hidden_by_pattern(std::wstring_view name) const noexcept;
    //! `upper_path` is a full folder path already upper-cased, without a trailing backslash.
    [[nodiscard]] bool hidden_by_path(std::wstring_view upper_path) const noexcept;

    [[nodiscard]] bool empty() const noexcept {
        return always_show.empty() && never_show.empty() && hide_patterns.empty() &&
               hide_paths.empty();
    }
};

} // namespace filetree::model
