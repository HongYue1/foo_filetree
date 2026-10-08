#pragma once

// Directory enumeration for one folder. Runs on a worker; never call it on the main thread.
// Pure Win32 + std, so the offline tests exercise exactly this code.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../model/extension_set.h"
#include "../model/filter_rules.h"
#include "../model/sort.h"
#include "../model/tree.h"

namespace filetree::fs {

enum class FileMode : std::uint8_t {
    all,      //!< every file
    playable, //!< files whose extension fb2k can play (or open as a playlist)
    none,     //!< folders only
};

struct EnumOptions {
    bool show_hidden{false};
    bool show_system{false};
    FileMode files{FileMode::playable};
    //! Used when files == playable. Null means "no filter known yet": every file is shown.
    std::shared_ptr<const model::ExtensionSet> playable;
    //! Always/never-shown extensions and hide patterns. Null: none.
    std::shared_ptr<const model::FilterRules> rules;
    model::SortOptions sort{};
};

//! The result of one enumeration: entries in display order, names in one shared buffer.
struct Listing {
    struct Item {
        std::uint32_t name_offset{};
        std::uint32_t name_length{};
        std::uint32_t attributes{};
        std::uint64_t size{};
        std::int64_t modified{};
    };

    std::vector<wchar_t> names;
    std::vector<Item> items;
    DWORD error{ERROR_SUCCESS}; //!< Win32 error; ERROR_SUCCESS for a listing (even an empty one)
    bool cancelled{false};

    [[nodiscard]] std::wstring_view name(const Item& item) const noexcept {
        return {names.data() + item.name_offset, item.name_length};
    }

    //! The listing as the tree consumes it. `out` is reused storage; views point into `names`.
    void to_records(std::vector<model::ChildRecord>& out) const;
};

//! Turns "C:\Music" into the pattern FindFirstFileExW wants ("C:\Music\*"), adding the "\\?\"
//! (or "\\?\UNC\") prefix when the result would exceed MAX_PATH.
[[nodiscard]] std::wstring make_search_pattern(std::wstring_view folder);

//! Enumerates `folder` (no trailing "\*"), filters and sorts per `options`. Checks `cancel`
//! between entries and returns early with `cancelled` set.
[[nodiscard]] Listing enumerate_folder(std::wstring_view folder, const EnumOptions& options,
                                       const std::atomic<bool>& cancel);

} // namespace filetree::fs
