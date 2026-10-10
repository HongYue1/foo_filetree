// Disk search: name matching and the walk over a folder tree (fs/disk_search.h).

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "check.h"

#include "../src/fs/disk_search.h"

using namespace filetree;

void test_disk_search(const std::filesystem::path& base) {
    bool glob = false;
    const std::wstring pattern = fs::search_pattern(L"  track ", glob);
    CHECK(pattern == L"TRACK" && !glob);
    CHECK(fs::name_matches(pattern, glob, L"Track 2.MP3"));
    CHECK(!fs::name_matches(pattern, glob, L"cover.jpg"));
    const std::wstring wild = fs::search_pattern(L"*.flac", glob);
    CHECK(glob && fs::name_matches(wild, glob, L"Track 10.FLAC"));
    CHECK(!fs::name_matches(wild, glob, L"Track 10.flac.txt"));
    CHECK(!fs::name_matches(L"", false, L"anything"));

    namespace stdfs = std::filesystem;
    const stdfs::path deep = base / L"Album 2" / L"Disc 1";
    stdfs::create_directories(deep);
    {
        HANDLE file = CreateFileW((deep / L"Deep track.mp3").c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    fs::EnumOptions options;
    options.files = fs::FileMode::all;
    std::atomic<bool> cancel{false};
    std::vector<fs::SearchHit> hits;
    int batches = 0;
    bool done = false;
    std::uint64_t folders = 0;
    bool limited = false;
    const auto collect = [&](fs::SearchBatch& batch) {
        ++batches;
        limited = batch.limited;
        for (auto& hit : batch.hits) hits.push_back(std::move(hit));
        done = batch.done;
        folders = batch.folders;
    };
    fs::search_folders({base.wstring()}, L"track", options, 100, cancel, collect);
    CHECK(done && !hits.empty());
    // Each folder's own hits first, then its subfolders in listing order.
    std::vector<std::wstring> names;
    for (const auto& hit : hits) names.push_back(stdfs::path(hit.path).filename().wstring());
    CHECK(names.size() == 3 && names[0] == L"Track 2.MP3" && names[1] == L"Track 10.flac" &&
          names[2] == L"Deep track.mp3");
    CHECK(hits.size() == 3 && hits[2].path == (deep / L"Deep track.mp3").wstring());
    CHECK(folders >= 5); // base, Album 10, Album 2, Disc 1, big

    // Folders match too; the limit stops early.
    hits.clear();
    fs::search_folders({base.wstring()}, L"album*", options, 1, cancel, collect);
    CHECK(done && limited && hits.size() == 1 && hits[0].path == (base / L"Album 2").wstring());

    // Cancelled before it starts: no batch at all.
    batches = 0;
    cancel = true;
    fs::search_folders({base.wstring()}, L"track", options, 100, cancel, collect);
    CHECK(batches == 0);

    std::error_code ignored;
    stdfs::remove_all(base / L"Album 2" / L"Disc 1", ignored);
}
