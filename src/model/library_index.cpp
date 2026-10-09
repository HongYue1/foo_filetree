#include "library_index.h"

#include <windows.h>

#include <algorithm>
#include <numeric>

namespace filetree::model {
namespace {

std::wstring upper(std::wstring_view text) {
    std::wstring out(text.size(), L'\0');
    if (text.empty()) return out;
    const int written = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, text.data(),
                                      static_cast<int>(text.size()), out.data(),
                                      static_cast<int>(out.size()), nullptr, nullptr, 0);
    out.resize(written > 0 ? static_cast<std::size_t>(written) : 0);
    return out;
}

bool ends_with_nocase(std::wstring_view text, std::wstring_view tail) noexcept {
    if (tail.size() > text.size()) return false;
    const std::wstring_view end = text.substr(text.size() - tail.size());
    return CompareStringOrdinal(end.data(), static_cast<int>(end.size()), tail.data(),
                                static_cast<int>(tail.size()), TRUE) == CSTR_EQUAL;
}

} // namespace

bool LibraryIndex::add(std::wstring_view path, std::wstring_view relative) {
    while (!relative.empty() && relative.front() == L'\\') relative.remove_prefix(1);
    if (relative.empty() || relative.size() >= path.size() || !ends_with_nocase(path, relative)) {
        return false;
    }
    std::wstring_view root = path.substr(0, path.size() - relative.size());
    if (root.empty() || root.back() != L'\\') return false;
    if (!(root.size() == 3 && root[1] == L':')) root.remove_suffix(1); // keep "D:\"

    // The track's folder, then its parents up to the root, until one is known already. Keys
    // have no trailing backslash ("D:" for a drive).
    const std::wstring_view folder = path.substr(0, path.find_last_of(L'\\'));
    if (!last_folder_.empty() && folder == last_folder_) return true;
    last_folder_.assign(folder);

    std::wstring root_key = upper(root);
    if (std::find(root_keys_.begin(), root_keys_.end(), root_key) == root_keys_.end()) {
        roots_.emplace_back(root);
        root_keys_.push_back(root_key);
    }
    if (root_key.size() > 1 && root_key.back() == L'\\') root_key.pop_back();
    scratch_ = upper(folder);
    while (scratch_.size() > root_key.size()) {
        if (!folders_.insert(scratch_).second) return true;
        const std::size_t slash = scratch_.find_last_of(L'\\');
        if (slash == std::wstring::npos) break;
        scratch_.resize(slash);
    }
    folders_.insert(root_key);
    return true;
}

void LibraryIndex::finish() {
    std::vector<std::size_t> order(roots_.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return root_keys_[a] < root_keys_[b]; });
    std::vector<std::wstring> roots;
    std::vector<std::wstring> keys;
    for (const std::size_t index : order) {
        roots.push_back(std::move(roots_[index]));
        keys.push_back(std::move(root_keys_[index]));
    }
    roots_.swap(roots);
    root_keys_.swap(keys);
    last_folder_.clear();
    last_folder_.shrink_to_fit();
    scratch_.clear();
    scratch_.shrink_to_fit();
}

bool LibraryIndex::holds_tracks(std::wstring_view upper_path) const noexcept {
    return folders_.find(upper_path) != folders_.end();
}

} // namespace filetree::model
