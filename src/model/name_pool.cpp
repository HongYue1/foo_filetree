#include "name_pool.h"

#include <cstring>

namespace filetree::model {

const wchar_t* NamePool::intern(std::wstring_view text) {
    const std::size_t needed = text.size() + 1;
    total_ += needed;

    if (needed > chunk_chars / 4) {
        // Unusually long (path components are <= 255, so this is a full path or bad data): give
        // it its own block, inserted before the current chunk so that chunk stays the open one.
        auto block = std::make_unique_for_overwrite<wchar_t[]>(needed);
        std::memcpy(block.get(), text.data(), text.size() * sizeof(wchar_t));
        block[text.size()] = L'\0';
        const wchar_t* result = block.get();
        chunks_.insert(chunks_.empty() ? chunks_.end() : chunks_.end() - 1, std::move(block));
        return result;
    }

    if (used_ + needed > chunk_chars) {
        chunks_.push_back(std::make_unique_for_overwrite<wchar_t[]>(chunk_chars));
        used_ = 0;
    }

    wchar_t* out = chunks_.back().get() + used_;
    std::memcpy(out, text.data(), text.size() * sizeof(wchar_t));
    out[text.size()] = L'\0';
    used_ += needed;
    return out;
}

} // namespace filetree::model
