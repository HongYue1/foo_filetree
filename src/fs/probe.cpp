#include "probe.h"

#include <vector>

#include "enumerate.h"

namespace filetree::fs {
namespace {

bool is_dot_entry(const wchar_t* name) noexcept {
    return name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
}

struct Pending {
    std::wstring path;
    unsigned depth;
};

} // namespace

Probe probe_folder(std::wstring_view folder, const ProbeFilter& filter, const ProbeLimits& limits,
                   const std::atomic<bool>& cancel) {
    if (filter.playable == nullptr) return Probe::unknown;
    const bool by_path = filter.rules != nullptr && !filter.rules->hide_paths.empty();
    std::vector<Pending> stack;
    stack.push_back({std::wstring(folder), 0});
    std::wstring upper;
    std::size_t entries = 0;
    bool complete = true;

    while (!stack.empty()) {
        const Pending current = std::move(stack.back());
        stack.pop_back();
        const std::wstring pattern = make_search_pattern(current.path);
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                       FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) complete = false; // denied, offline
            continue;
        }
        do {
            if (cancel.load(std::memory_order_relaxed) || ++entries > limits.entries) {
                FindClose(find);
                return Probe::unknown;
            }
            if (is_dot_entry(data.cFileName)) continue;
            const DWORD attributes = data.dwFileAttributes;
            if (!filter.show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) continue;
            if (!filter.show_system && (attributes & FILE_ATTRIBUTE_SYSTEM) != 0) continue;
            if (filter.rules != nullptr && filter.rules->hidden_by_pattern(data.cFileName)) continue;
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                if (filter.rules != nullptr && filter.rules->never_show.matches_file(data.cFileName)) {
                    continue;
                }
                if (filter.playable->matches_file(data.cFileName)) {
                    FindClose(find);
                    return Probe::playable;
                }
                continue;
            }
            // A junction or symbolic link may loop or lead anywhere: not followed, not empty.
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 || current.depth >= limits.depth) {
                complete = false;
                continue;
            }
            std::wstring child = current.path;
            if (child.empty() || child.back() != L'\\') child.push_back(L'\\');
            child += data.cFileName;
            if (by_path) {
                upper.resize(child.size() + 1);
                upper.resize(model::to_upper(child, upper.data(), upper.size()));
                if (filter.rules->hidden_by_path(upper)) continue;
            }
            stack.push_back({std::move(child), current.depth + 1});
        } while (FindNextFileW(find, &data) != FALSE);
        FindClose(find);
    }
    return complete ? Probe::empty : Probe::unknown;
}

bool EmptyFolders::contains(std::wstring_view upper_path) const {
    std::lock_guard lock(mutex_);
    return paths_.find(upper_path) != paths_.end();
}

void EmptyFolders::set(std::wstring_view upper_path, bool empty) {
    std::lock_guard lock(mutex_);
    if (!empty) {
        if (const auto found = paths_.find(upper_path); found != paths_.end()) paths_.erase(found);
        return;
    }
    if (paths_.size() >= max_entries) paths_.clear();
    paths_.emplace(upper_path);
}

void EmptyFolders::clear() {
    std::lock_guard lock(mutex_);
    paths_.clear();
}

} // namespace filetree::fs
