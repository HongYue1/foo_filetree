#include "extension_set.h"

#include <windows.h>

#include "sort.h"

namespace filetree::model {
namespace {

//! Lower-cases into `buffer` (invariant locale). Returns the lowered view, or empty if too long.
std::wstring_view lower(std::wstring_view text,
                        wchar_t (&buffer)[ExtensionSet::max_extension + 1]) noexcept {
    if (text.empty() || text.size() > ExtensionSet::max_extension) return {};
    const int written =
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(),
                      static_cast<int>(text.size()), buffer, ExtensionSet::max_extension,
                      nullptr, nullptr, 0);
    if (written <= 0) return {};
    return {buffer, static_cast<std::size_t>(written)};
}

std::wstring_view trim(std::wstring_view text) noexcept {
    while (!text.empty() && (text.front() == L' ' || text.front() == L'\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == L' ' || text.back() == L'\t')) text.remove_suffix(1);
    return text;
}

} // namespace

void ExtensionSet::add(std::wstring_view extension) {
    extension = trim(extension);
    if (extension.starts_with(L"*")) extension.remove_prefix(1);
    if (extension.starts_with(L".")) extension.remove_prefix(1);
    if (extension.find_first_of(L"*?.\\/") != std::wstring_view::npos) return;

    wchar_t buffer[max_extension + 1];
    if (const std::wstring_view lowered = lower(extension, buffer); !lowered.empty()) {
        set_.emplace(lowered);
    }
}

void ExtensionSet::add_mask(std::wstring_view mask) {
    while (!mask.empty()) {
        const std::size_t end = mask.find_first_of(L";,|");
        add(mask.substr(0, end));
        if (end == std::wstring_view::npos) break;
        mask.remove_prefix(end + 1);
    }
}

bool ExtensionSet::contains(std::wstring_view extension) const noexcept {
    wchar_t buffer[max_extension + 1];
    const std::wstring_view lowered = lower(extension, buffer);
    return !lowered.empty() && set_.find(lowered) != set_.end();
}

bool ExtensionSet::matches_file(std::wstring_view file_name) const noexcept {
    return contains(extension_of(file_name));
}

} // namespace filetree::model
