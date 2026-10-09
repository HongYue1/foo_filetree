#pragma once

// Which icon a file gets, from its extension only (no disk access, no allocation). Audio is
// decided by the caller from foobar2000's playable types; this covers the other common kinds.

#include <cstdint>
#include <string_view>

namespace filetree::model {

enum class FileKind : std::uint8_t { other, video, image, text, pdf, playlist };

//! The kind for an extension without the dot ("mkv", "JPG"), case-insensitive.
[[nodiscard]] FileKind file_kind(std::wstring_view extension) noexcept;

} // namespace filetree::model
