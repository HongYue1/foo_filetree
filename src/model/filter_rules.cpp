#include "filter_rules.h"

#include <windows.h>

#include <algorithm>

namespace filetree::model {

bool glob_match(std::wstring_view pattern, std::wstring_view name) noexcept {
    std::size_t p = 0;
    std::size_t n = 0;
    std::size_t star = std::wstring_view::npos; // pattern position after the last '*'
    std::size_t resume = 0;                     // name position that '*' currently covers to
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == L'?' || pattern[p] == name[n])) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == L'*') {
            star = ++p;
            resume = n;
        } else if (star != std::wstring_view::npos) {
            p = star;
            n = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == L'*') ++p;
    return p == pattern.size();
}

std::size_t to_upper(std::wstring_view text, wchar_t* out, std::size_t capacity) noexcept {
    const std::size_t length = std::min(text.size(), capacity);
    if (length == 0) return 0;
    const int written = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, text.data(),
                                      static_cast<int>(length), out, static_cast<int>(capacity),
                                      nullptr, nullptr, 0);
    return written > 0 ? static_cast<std::size_t>(written) : 0;
}

void FilterRules::set_hide_patterns(std::wstring_view list) {
    hide_patterns.clear();
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t end = list.find_first_of(L";|,\r\n", start);
        if (end == std::wstring_view::npos) end = list.size();
        std::wstring_view item = list.substr(start, end - start);
        while (!item.empty() && (item.front() == L' ' || item.front() == L'\t')) item.remove_prefix(1);
        while (!item.empty() && (item.back() == L' ' || item.back() == L'\t')) item.remove_suffix(1);
        if (!item.empty() && item.size() <= 255) {
            wchar_t buffer[256];
            const std::size_t length = to_upper(item, buffer, std::size(buffer));
            if (length > 0) hide_patterns.emplace_back(buffer, length);
        }
        start = end + 1;
    }
}

namespace {

//! "%ProgramFiles%" in a 32-bit process names "Program Files (x86)"; a user listing it means the
//! 64-bit folder, which ProgramW6432 names (set only on 64-bit Windows).
std::wstring expand_path(std::wstring_view entry) {
    std::wstring text(entry);
    constexpr std::wstring_view program_files = L"%PROGRAMFILES%";
    wchar_t native[MAX_PATH];
    if (text.size() >= program_files.size() &&
        CompareStringOrdinal(text.c_str(), static_cast<int>(program_files.size()),
                             program_files.data(), static_cast<int>(program_files.size()),
                             TRUE) == CSTR_EQUAL) {
        const DWORD length = GetEnvironmentVariableW(L"ProgramW6432", native, MAX_PATH);
        if (length > 0 && length < MAX_PATH) text.replace(0, program_files.size(), native, length);
    }
    if (text.find(L'%') == std::wstring::npos) return text;
    wchar_t buffer[1024];
    const DWORD length = ExpandEnvironmentStringsW(text.c_str(), buffer, 1024);
    if (length == 0 || length > 1024) return {};
    std::wstring out(buffer, length - 1); // length counts the terminator
    if (out.find(L'%') != std::wstring::npos) return {}; // a variable that is not set
    return out;
}

} // namespace

void FilterRules::set_hide_paths(const std::vector<std::wstring>& paths) {
    hide_paths.clear();
    for (const std::wstring& entry : paths) {
        std::wstring path = expand_path(entry);
        while (path.size() > 1 && path.back() == L'\\') path.pop_back();
        if (path.empty() || path.size() > 1000) continue;
        wchar_t buffer[1024];
        const std::size_t length = to_upper(path, buffer, std::size(buffer));
        if (length > 0) hide_paths.emplace_back(buffer, length);
    }
}

bool FilterRules::hidden_by_path(std::wstring_view upper_path) const noexcept {
    for (const std::wstring& pattern : hide_paths) {
        if (glob_match(pattern, upper_path)) return true;
    }
    return false;
}

bool FilterRules::hidden_by_pattern(std::wstring_view name) const noexcept {
    if (hide_patterns.empty()) return false;
    wchar_t buffer[260];
    const std::size_t length = to_upper(name, buffer, std::size(buffer));
    const std::wstring_view upper(buffer, length);
    for (const std::wstring& pattern : hide_patterns) {
        if (glob_match(pattern, upper)) return true;
    }
    return false;
}

} // namespace filetree::model
