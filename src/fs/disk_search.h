#pragma once

// Searching whole folder trees by name (Ctrl+Shift+F, settings disk_search). Runs on a worker;
// pure Win32 + std, so the offline tests exercise exactly this code.
//
// Each folder is listed with enumerate_folder and the panel's options, so the same files and
// folders are found as the tree would show (hidden, system, file mode, Files tab rules, hidden
// folders). Names match like the filter box: with * or ? the whole name, otherwise any part,
// without case.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "enumerate.h"

namespace filetree::fs {

struct SearchHit {
    std::wstring path; //!< full path of the matching file or folder
    std::uint32_t attributes{};
    std::uint64_t size{};
    std::int64_t modified{};
};

//! What the worker found since the last batch.
struct SearchBatch {
    std::vector<SearchHit> hits;
    std::uint64_t folders{}; //!< folders searched so far (all batches)
    bool done{false};        //!< the last batch: every root was searched (or `limited`)
    bool limited{false};     //!< stopped at the hit limit
};

//! The search text upper-cased for name_matches; `glob` is set when it has * or ?.
[[nodiscard]] std::wstring search_pattern(std::wstring_view text, bool& glob);
//! `pattern` from search_pattern.
[[nodiscard]] bool name_matches(std::wstring_view pattern, bool glob,
                                std::wstring_view name) noexcept;

//! Searches below each of `roots` (the roots themselves are not candidates), depth first in
//! listing order. Junctions and symbolic links are not followed (no loops). Hands batches to
//! `emit` on this thread at most every `batch_ms`, and a last one with `done` set unless
//! `cancel` was raised. Stops after `limit` hits.
void search_folders(const std::vector<std::wstring>& roots, std::wstring_view text,
                    const EnumOptions& options, std::size_t limit,
                    const std::atomic<bool>& cancel,
                    const std::function<void(SearchBatch&)>& emit, unsigned batch_ms = 150);

} // namespace filetree::fs
