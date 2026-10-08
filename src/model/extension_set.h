#pragma once

// A set of file extensions, matched case-insensitively. Built once (main thread), then shared
// read-only with workers as shared_ptr<const ExtensionSet>. Lookups never allocate.

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>

namespace filetree::model {

class ExtensionSet {
public:
    //! Longest extension we store or look up. Longer ones never match.
    static constexpr std::size_t max_extension = 31;

    //! Adds one extension: "flac", ".flac" or "*.flac". Wildcards elsewhere are ignored.
    void add(std::wstring_view extension);

    //! Adds a mask list as fb2k's input_file_type reports it: "*.MP3;*.MP2". Also accepts ',' and
    //! '|' as separators and surrounding spaces, for user-typed lists.
    void add_mask(std::wstring_view mask);

    [[nodiscard]] bool contains(std::wstring_view extension) const noexcept;

    //! True when the file name's last extension is in the set.
    [[nodiscard]] bool matches_file(std::wstring_view file_name) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return set_.size(); }
    [[nodiscard]] bool empty() const noexcept { return set_.empty(); }

private:
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::wstring_view text) const noexcept {
            return std::hash<std::wstring_view>{}(text);
        }
    };

    std::unordered_set<std::wstring, Hash, std::equal_to<>> set_;
};

} // namespace filetree::model
