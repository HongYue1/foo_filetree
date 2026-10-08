#pragma once

// Sort order for folder listings. Pure Win32 (no fb2k), so it runs on workers and in the offline
// tests. Listings are sorted once on the worker that enumerated them; the main thread never sorts.

#include <windows.h>

#include <cstdint>
#include <string_view>

namespace filetree::model {

enum class SortField : std::uint8_t {
    name_natural, //!< "Track 2" before "Track 10", case-insensitive, user locale
    name,         //!< plain case-insensitive name (ordinal)
    modified,     //!< last write time, then natural name
    size,         //!< file size, then natural name (folders: name only)
    type,         //!< extension, then natural name
};

struct SortOptions {
    SortField field{SortField::name_natural};
    bool folders_first{true};
    bool reverse{false}; //!< reverses within the folder and file groups; folders stay first
};

//! What the comparator needs from an entry. Cheap to build: a view and three integers.
struct SortItem {
    std::wstring_view name;
    std::uint32_t attributes{};
    std::uint64_t size{};
    std::int64_t modified{}; //!< FILETIME as a 64-bit integer
};

//! Natural, case-insensitive, user-locale comparison. Equal names under that rule fall back to an
//! ordinal comparison so the order is total and deterministic. Returns <0, 0 or >0.
[[nodiscard]] int compare_natural(std::wstring_view a, std::wstring_view b) noexcept;

//! Ordinal case-insensitive comparison, ordinal case-sensitive tie-break. Returns <0, 0 or >0.
[[nodiscard]] int compare_name(std::wstring_view a, std::wstring_view b) noexcept;

//! The extension of a file name without the dot ("flac"), empty if none. A leading dot alone
//! (".nomedia") is a name, not an extension.
[[nodiscard]] std::wstring_view extension_of(std::wstring_view name) noexcept;

[[nodiscard]] inline bool is_folder(std::uint32_t attributes) noexcept {
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

//! Strict weak ordering for std::sort.
[[nodiscard]] bool sort_less(const SortItem& a, const SortItem& b,
                             const SortOptions& options) noexcept;

} // namespace filetree::model
