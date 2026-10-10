#include "disk_search.h"

#include "../model/filter_rules.h"

namespace filetree::fs {

std::wstring search_pattern(std::wstring_view text, bool& glob) {
    while (!text.empty() && text.front() == L' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == L' ') text.remove_suffix(1);
    wchar_t upper[256];
    const std::size_t length = model::to_upper(text, upper, std::size(upper));
    std::wstring out(upper, length);
    glob = out.find_first_of(L"*?") != std::wstring::npos;
    return out;
}

bool name_matches(std::wstring_view pattern, bool glob, std::wstring_view name) noexcept {
    if (pattern.empty()) return false;
    wchar_t upper[256];
    const std::size_t length = model::to_upper(name, upper, std::size(upper));
    const std::wstring_view view(upper, length);
    return glob ? model::glob_match(pattern, view) : view.find(pattern) != std::wstring_view::npos;
}

void search_folders(const std::vector<std::wstring>& roots, std::wstring_view text,
                    const EnumOptions& options, std::size_t limit,
                    const std::atomic<bool>& cancel,
                    const std::function<void(SearchBatch&)>& emit, unsigned batch_ms) {
    bool glob = false;
    const std::wstring pattern = search_pattern(text, glob);
    SearchBatch batch;
    std::size_t found = 0;
    ULONGLONG last_emit = GetTickCount64();
    const auto stopped = [&] { return cancel.load(std::memory_order_relaxed); };
    const auto flush = [&](bool done) {
        batch.done = done;
        emit(batch);
        batch.hits.clear();
        last_emit = GetTickCount64();
    };

    EnumOptions listing_options = options;
    listing_options.pinned = nullptr;
    listing_options.probe = false;
    listing_options.hide_empty = false; // a probe per folder would make it far slower

    std::vector<std::wstring> stack; // folders still to list; the next one is at the back
    std::vector<std::wstring> children;
    for (const std::wstring& root : roots) {
        if (pattern.empty()) break;
        stack.assign(1, root);
        while (!stack.empty() && !stopped()) {
            std::wstring folder = std::move(stack.back());
            stack.pop_back();
            const Listing listing = enumerate_folder(folder, listing_options, cancel);
            ++batch.folders;
            if (listing.cancelled) break;
            if (!folder.empty() && folder.back() != L'\\') folder.push_back(L'\\');
            children.clear();
            for (const Listing::Item& item : listing.items) {
                const std::wstring_view name = listing.name(item);
                if (name_matches(pattern, glob, name)) {
                    batch.hits.push_back({folder + std::wstring(name), item.attributes, item.size,
                                          item.modified});
                    if (++found >= limit) {
                        batch.limited = true;
                        flush(true);
                        return;
                    }
                }
                const bool walk = (item.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                                  (item.attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
                if (walk) children.push_back(folder + std::wstring(name));
            }
            // Reversed onto the stack, so the first child is listed next (listing order).
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push_back(std::move(*it));
            }
            if (GetTickCount64() - last_emit >= batch_ms) flush(false);
        }
    }
    if (!stopped()) flush(true);
}

} // namespace filetree::fs
