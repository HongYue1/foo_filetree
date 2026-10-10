#pragma once

// "Does this folder hold anything playable?" for hiding empty folders. Runs on a worker as part
// of a probe listing (EnumOptions::probe). Depth-first with an early exit at the first playable
// file and a budget, so a huge tree costs at most a few thousand directory entries. Pure Win32 +
// std, tested offline.

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include "../model/extension_set.h"
#include "../model/filter_rules.h"

namespace filetree::fs {

enum class Probe : std::uint8_t {
    playable, //!< a playable file somewhere below
    empty,    //!< searched everything: nothing playable
    unknown,  //!< over budget, unreadable, a link, or cancelled: keep it visible
};

struct ProbeLimits {
    std::size_t entries{5000}; //!< directory entries read in total
    unsigned depth{8};         //!< levels below the folder
};

//! What the search skips, as the listing would: hidden / system entries, hide rules.
struct ProbeFilter {
    bool show_hidden{false};
    bool show_system{false};
    const model::ExtensionSet* playable{};  //!< required
    const model::FilterRules* rules{};      //!< may be null
};

[[nodiscard]] Probe probe_folder(std::wstring_view folder, const ProbeFilter& filter,
                                 const ProbeLimits& limits, const std::atomic<bool>& cancel);

//! Folders found empty, shared by every panel and worker: a listing leaves them out at once and
//! the probe that follows corrects it. Upper-cased paths, no trailing backslash. Thread-safe.
class EmptyFolders {
public:
    [[nodiscard]] bool contains(std::wstring_view upper_path) const;
    void set(std::wstring_view upper_path, bool empty);
    void clear();

private:
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::wstring_view text) const noexcept {
            return std::hash<std::wstring_view>{}(text);
        }
    };
    static constexpr std::size_t max_entries = 50000; //!< then start over (a few MB at most)

    mutable std::mutex mutex_;
    std::unordered_set<std::wstring, Hash, std::equal_to<>> paths_;
};

} // namespace filetree::fs
