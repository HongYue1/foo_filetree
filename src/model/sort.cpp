#include "sort.h"

namespace filetree::model {
namespace {

int clamp_length(std::size_t length) noexcept {
    // Path components are at most 255 characters; this only guards the int conversion.
    return length > 0x7fffffff ? 0x7fffffff : static_cast<int>(length);
}

// The Win32 compare functions fail (return 0) on an empty or null string, so empties are ordered
// here: an empty string sorts before any non-empty one.
int compare_empty(std::wstring_view a, std::wstring_view b) noexcept {
    return a.empty() ? (b.empty() ? 0 : -1) : 1;
}

int ordinal(std::wstring_view a, std::wstring_view b, bool ignore_case) noexcept {
    if (a.empty() || b.empty()) return compare_empty(a, b);
    const int result = CompareStringOrdinal(a.data(), clamp_length(a.size()), b.data(),
                                            clamp_length(b.size()), ignore_case ? TRUE : FALSE);
    return result == 0 ? 0 : result - CSTR_EQUAL;
}

template <typename T>
int three_way(T a, T b) noexcept {
    return a < b ? -1 : (b < a ? 1 : 0);
}

} // namespace

int compare_natural(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.empty() || b.empty()) return compare_empty(a, b);
    const int result = CompareStringEx(LOCALE_NAME_USER_DEFAULT,
                                       LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS, a.data(),
                                       clamp_length(a.size()), b.data(), clamp_length(b.size()),
                                       nullptr, nullptr, 0);
    if (result != 0 && result != CSTR_EQUAL) return result - CSTR_EQUAL;
    // Equal (or the call failed): fall back to ordinal so the order is total.
    if (const int folded = ordinal(a, b, true); folded != 0) return folded;
    return ordinal(a, b, false);
}

int compare_name(std::wstring_view a, std::wstring_view b) noexcept {
    if (const int folded = ordinal(a, b, true); folded != 0) return folded;
    return ordinal(a, b, false);
}

std::wstring_view extension_of(std::wstring_view name) noexcept {
    const std::size_t dot = name.rfind(L'.');
    if (dot == std::wstring_view::npos || dot == 0 || dot + 1 == name.size()) return {};
    return name.substr(dot + 1);
}

bool sort_less(const SortItem& a, const SortItem& b, const SortOptions& options) noexcept {
    const bool a_folder = is_folder(a.attributes);
    const bool b_folder = is_folder(b.attributes);
    if (options.folders_first && a_folder != b_folder) return a_folder;

    int order = 0;
    switch (options.field) {
    case SortField::name_natural:
        break;
    case SortField::name:
        order = compare_name(a.name, b.name);
        break;
    case SortField::modified:
        order = three_way(a.modified, b.modified);
        break;
    case SortField::size:
        // Folders have no meaningful size here; they sort by name among themselves.
        if (!a_folder && !b_folder) order = three_way(a.size, b.size);
        break;
    case SortField::type:
        if (!a_folder && !b_folder) order = compare_name(extension_of(a.name), extension_of(b.name));
        break;
    }
    if (order == 0) order = compare_natural(a.name, b.name);
    return options.reverse ? order > 0 : order < 0;
}

} // namespace filetree::model
