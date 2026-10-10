// Hiding folders without playable files: the probe itself, and the probe listing with the
// shared cache and the library shortcut.

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "check.h"

#include "../src/fs/enumerate.h"
#include "../src/fs/probe.h"

using namespace filetree;

namespace {

void make_file(const std::filesystem::path& path) { std::ofstream(path) << "x"; }

//! Probe listings keep folders changed in the last minutes; make these an hour old.
void age(const std::filesystem::path& folder) {
    HANDLE handle = CreateFileW(folder.c_str(), FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER value{};
    value.LowPart = now.dwLowDateTime;
    value.HighPart = now.dwHighDateTime;
    value.QuadPart -= 3600ULL * 10'000'000;
    const FILETIME old{value.LowPart, value.HighPart};
    SetFileTime(handle, nullptr, nullptr, &old);
    CloseHandle(handle);
}

std::vector<std::wstring> names_of(const fs::Listing& listing) {
    std::vector<std::wstring> names;
    for (const auto& item : listing.items) names.emplace_back(listing.name(item));
    return names;
}

std::wstring upper(const std::wstring& text) {
    wchar_t buffer[1024];
    return {buffer, model::to_upper(text, buffer, 1024)};
}

} // namespace

void test_probe(const std::filesystem::path& base) {
    namespace stdfs = std::filesystem;
    const stdfs::path root = base / L"probe";
    stdfs::create_directories(root / L"empty" / L"deeper");
    make_file(root / L"empty" / L"deeper" / L"notes.txt");
    stdfs::create_directories(root / L"nested" / L"a" / L"b");
    make_file(root / L"nested" / L"a" / L"b" / L"song.flac");
    stdfs::create_directories(root / L"album");
    make_file(root / L"album" / L"one.mp3");
    stdfs::create_directories(root / L"blank");
    stdfs::create_directories(root / L"library");
    for (const wchar_t* name : {L"empty", L"nested", L"album", L"blank", L"library"}) {
        age(root / name);
    }

    auto playable = std::make_shared<model::ExtensionSet>();
    playable->add_mask(L"*.flac;*.mp3");
    std::atomic<bool> cancel{false};
    const fs::ProbeFilter filter{false, false, playable.get(), nullptr};
    const auto probe = [&](const wchar_t* name, const fs::ProbeFilter& with,
                           fs::ProbeLimits limits = {}) {
        return fs::probe_folder((root / name).wstring(), with, limits, cancel);
    };
    CHECK(probe(L"empty", filter) == fs::Probe::empty);
    CHECK(probe(L"blank", filter) == fs::Probe::empty);
    CHECK(probe(L"nested", filter) == fs::Probe::playable);
    CHECK(probe(L"nested", filter, {5000, 1}) == fs::Probe::unknown); // too deep
    CHECK(probe(L"empty", filter, {1, 8}) == fs::Probe::unknown);     // over budget
    CHECK(probe(L"missing", filter) == fs::Probe::unknown); // unreadable: keep it
    const fs::ProbeFilter no_types{false, false, nullptr, nullptr};
    CHECK(probe(L"album", no_types) == fs::Probe::unknown);
    // A hide rule applies inside the search too.
    model::FilterRules rules;
    rules.set_hide_patterns(L"a");
    const fs::ProbeFilter hiding{false, false, playable.get(), &rules};
    CHECK(probe(L"nested", hiding) == fs::Probe::empty);

    // The probe listing drops the empty folders and remembers them; a library folder is kept
    // without a search.
    model::LibraryIndex library;
    CHECK(library.add((root / L"library" / L"gone.mp3").wstring(), L"library\\gone.mp3"));
    library.finish();
    fs::EnumOptions options;
    options.files = fs::FileMode::none;
    options.hide_empty = true;
    options.probe = true;
    options.probe_types = playable;
    options.empty = std::make_shared<fs::EmptyFolders>();
    options.library = std::make_shared<model::LibraryIndex>(std::move(library));
    const fs::Listing probed = fs::enumerate_folder(root.wstring(), options, cancel);
    const std::vector<std::wstring> kept{L"album", L"library", L"nested"};
    CHECK(probed.probed);
    CHECK(names_of(probed) == kept);
    CHECK(options.empty->contains(upper((root / L"empty").wstring())));
    CHECK(options.empty->contains(upper((root / L"blank").wstring())));
    CHECK(!options.empty->contains(upper((root / L"album").wstring())));

    // A plain listing leaves the known empty folders out without searching.
    options.probe = false;
    const fs::Listing quick = fs::enumerate_folder(root.wstring(), options, cancel);
    CHECK(!quick.probed);
    CHECK(names_of(quick) == kept);

    // A folder that just changed is kept, whatever is in it.
    stdfs::create_directories(root / L"fresh");
    options.probe = true;
    const std::vector<std::wstring> with_fresh{L"album", L"fresh", L"library", L"nested"};
    CHECK(names_of(fs::enumerate_folder(root.wstring(), options, cancel)) == with_fresh);

    options.empty->set(upper((root / L"blank").wstring()), false);
    CHECK(!options.empty->contains(upper((root / L"blank").wstring())));
    options.empty->clear();
    CHECK(!options.empty->contains(upper((root / L"empty").wstring())));
}
