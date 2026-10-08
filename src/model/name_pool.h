#pragma once

// Append-only UTF-16 string arena for node names. Stable pointers, null-terminated, one heap
// allocation per 32K characters instead of one per name. Main thread only.

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace filetree::model {

class NamePool {
public:
    static constexpr std::size_t chunk_chars = 32 * 1024;

    //! Copies `text` into the pool and returns a stable, null-terminated pointer.
    const wchar_t* intern(std::wstring_view text);

    void clear() noexcept {
        chunks_.clear();
        used_ = chunk_chars;
        total_ = 0;
    }

    //! Characters stored, terminators included. For the memory counters (M7).
    [[nodiscard]] std::size_t size_chars() const noexcept { return total_; }

private:
    std::vector<std::unique_ptr<wchar_t[]>> chunks_;
    std::size_t used_{chunk_chars}; //!< used characters in the last chunk
    std::size_t total_{};
};

} // namespace filetree::model
