#include "enumerate.h"

#include <algorithm>
#include <numeric>

#include "probe.h"

namespace filetree::fs {
namespace {

bool is_dot_entry(const wchar_t* name) noexcept {
    return name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
}

std::int64_t to_int64(const FILETIME& time) noexcept {
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(time.dwHighDateTime) << 32) |
                                     time.dwLowDateTime);
}

bool keep(const WIN32_FIND_DATAW& data, const EnumOptions& options) noexcept {
    const DWORD attributes = data.dwFileAttributes;
    if (!options.show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) return false;
    if (!options.show_system && (attributes & FILE_ATTRIBUTE_SYSTEM) != 0) return false;
    const model::FilterRules* rules = options.rules.get();
    if (rules != nullptr && rules->hidden_by_pattern(data.cFileName)) return false;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return true;
    if (options.files == FileMode::none) return false;
    if (rules != nullptr) {
        if (rules->never_show.matches_file(data.cFileName)) return false;
        if (rules->always_show.matches_file(data.cFileName)) return true;
    }

    switch (options.files) {
    case FileMode::all:
        return true;
    case FileMode::none:
        return false;
    case FileMode::playable:
        return options.playable == nullptr || options.playable->matches_file(data.cFileName);
    }
    return true;
}

//! The probe step: searches each child folder, drops the empty ones, updates the shared cache.
void drop_empty_folders(Listing& listing, std::wstring_view folder, std::wstring& upper_path,
                        std::size_t prefix, const EnumOptions& options,
                        const std::atomic<bool>& cancel) {
    constexpr std::int64_t keep_recent = 3LL * 60 * 10'000'000; // 3 minutes in FILETIME units
    FILETIME now_time{};
    GetSystemTimeAsFileTime(&now_time);
    const std::int64_t now = to_int64(now_time);
    const ProbeFilter filter{options.show_hidden, options.show_system, options.probe_types.get(),
                             options.rules.get()};
    std::wstring path(folder);
    if (path.empty() || path.back() != L'\\') path.push_back(L'\\');
    const std::size_t path_prefix = path.size();

    std::erase_if(listing.items, [&](const Listing::Item& item) {
        if ((item.attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || listing.cancelled) return false;
        if (cancel.load(std::memory_order_relaxed)) {
            listing.cancelled = true;
            return false;
        }
        const std::wstring_view name = listing.name(item);
        const std::size_t length = model::to_upper(name, upper_path.data() + prefix, MAX_PATH);
        const std::wstring_view upper(upper_path.data(), prefix + length);
        Probe found = Probe::unknown;
        if (now - item.modified >= keep_recent) {
            if (options.library != nullptr && options.library->holds_tracks(upper)) {
                found = Probe::playable;
            } else {
                path.resize(path_prefix);
                path.append(name);
                found = probe_folder(path, filter, ProbeLimits{}, cancel);
            }
        }
        if (cancel.load(std::memory_order_relaxed)) {
            listing.cancelled = true;
            return false;
        }
        if (options.empty != nullptr) options.empty->set(upper, found == Probe::empty);
        return found == Probe::empty;
    });
    listing.probed = true;
}

} // namespace

void Listing::to_records(std::vector<model::ChildRecord>& out) const {
    out.clear();
    out.reserve(items.size());
    for (const Item& item : items) {
        out.push_back({name(item), item.attributes, item.size, item.modified});
    }
}

std::wstring make_search_pattern(std::wstring_view folder) {
    std::wstring pattern;
    const bool already_prefixed = folder.starts_with(L"\\\\?\\");
    const bool unc = !already_prefixed && folder.starts_with(L"\\\\");
    const std::size_t length = folder.size() + 2;
    pattern.reserve(length + 8);

    if (!already_prefixed && length >= MAX_PATH) {
        if (unc) {
            pattern = L"\\\\?\\UNC\\";
            pattern.append(folder.substr(2));
        } else {
            pattern = L"\\\\?\\";
            pattern.append(folder);
        }
    } else {
        pattern.assign(folder);
    }
    if (!pattern.empty() && pattern.back() != L'\\') pattern.push_back(L'\\');
    pattern.push_back(L'*');
    return pattern;
}

Listing look_up_files(const std::vector<std::wstring>& paths, const std::atomic<bool>& cancel) {
    Listing listing;
    for (const std::wstring& path : paths) {
        if (cancel.load(std::memory_order_relaxed)) {
            listing.cancelled = true;
            return listing;
        }
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) == FALSE) continue;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        Listing::Item item;
        item.name_offset = static_cast<std::uint32_t>(listing.names.size());
        item.name_length = static_cast<std::uint32_t>(path.size());
        item.attributes = data.dwFileAttributes;
        item.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        item.modified = to_int64(data.ftLastWriteTime);
        listing.names.insert(listing.names.end(), path.begin(), path.end());
        listing.items.push_back(item);
    }
    return listing;
}

Listing enumerate_folder(std::wstring_view folder, const EnumOptions& options,
                         const std::atomic<bool>& cancel) {
    if (options.pinned != nullptr) return look_up_files(*options.pinned, cancel);
    Listing listing;
    const std::wstring pattern = make_search_pattern(folder);

    // Basic info skips the 8.3 short name; large fetch asks the file system for bigger batches.
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
                                   nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        // An empty folder (or an empty drive root) is a valid, empty listing.
        listing.error = error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
        return listing;
    }

    // Folders hidden by path or known empty: "<FOLDER>\" upper-cased once, each name appended
    // in place.
    const model::FilterRules* rules = options.rules.get();
    const bool by_path = rules != nullptr && !rules->hide_paths.empty();
    const bool hide_empty = options.hide_empty && options.probe_types != nullptr;
    EmptyFolders* const known_empty = hide_empty && !options.probe ? options.empty.get() : nullptr;
    std::wstring upper_path;
    std::size_t prefix = 0;
    if (by_path || hide_empty) {
        upper_path.resize(folder.size() + 1 + MAX_PATH);
        prefix = model::to_upper(folder, upper_path.data(), folder.size());
        if (prefix == 0 || upper_path[prefix - 1] != L'\\') upper_path[prefix++] = L'\\';
    }

    listing.names.reserve(4096);
    do {
        if (cancel.load(std::memory_order_relaxed)) {
            listing.cancelled = true;
            break;
        }
        if (is_dot_entry(data.cFileName) || !keep(data, options)) continue;

        const std::size_t name_length = wcsnlen(data.cFileName, MAX_PATH);
        if (prefix != 0 && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            const std::size_t length = model::to_upper({data.cFileName, name_length},
                                                       upper_path.data() + prefix, MAX_PATH);
            const std::wstring_view upper(upper_path.data(), prefix + length);
            if (by_path && rules->hidden_by_path(upper)) continue;
            if (known_empty != nullptr && known_empty->contains(upper)) {
                ++listing.skipped_empty;
                continue;
            }
        }
        Listing::Item item;
        item.name_offset = static_cast<std::uint32_t>(listing.names.size());
        item.name_length = static_cast<std::uint32_t>(name_length);
        item.attributes = data.dwFileAttributes;
        item.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        item.modified = to_int64(data.ftLastWriteTime);
        listing.names.insert(listing.names.end(), data.cFileName, data.cFileName + name_length);
        listing.items.push_back(item);
    } while (FindNextFileW(find, &data) != FALSE);

    if (!listing.cancelled) {
        const DWORD error = GetLastError();
        if (error != ERROR_NO_MORE_FILES && error != ERROR_SUCCESS) listing.error = error;
    }
    FindClose(find);
    if (listing.cancelled) return listing;
    if (hide_empty && options.probe) {
        drop_empty_folders(listing, folder, upper_path, prefix, options, cancel);
        if (listing.cancelled) return listing;
    }

    // Sort an index permutation (moves 4 bytes per swap), then apply it once.
    std::vector<std::uint32_t> order(listing.items.size());
    std::iota(order.begin(), order.end(), 0u);
    const auto item_of = [&](std::uint32_t index) {
        const Listing::Item& item = listing.items[index];
        return model::SortItem{listing.name(item), item.attributes, item.size, item.modified};
    };
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        return model::sort_less(item_of(a), item_of(b), options.sort);
    });

    std::vector<Listing::Item> sorted;
    sorted.reserve(listing.items.size());
    for (const std::uint32_t index : order) sorted.push_back(listing.items[index]);
    listing.items.swap(sorted);
    return listing;
}

} // namespace filetree::fs
