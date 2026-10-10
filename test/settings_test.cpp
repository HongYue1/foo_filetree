// Offline tests for model/filter_rules and settings/settings_model.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "../src/actions/presets.h"
#include "../src/fs/enumerate.h"
#include "../src/fs/watcher.h"
#include "../src/model/filter_rules.h"
#include "../src/settings/panel_state.h"
#include "../src/settings/settings_model.h"

using namespace filetree;

namespace {

bool glob(const wchar_t* pattern, const wchar_t* name) {
    wchar_t p[256];
    wchar_t n[256];
    const auto pl = model::to_upper(pattern, p, 256);
    const auto nl = model::to_upper(name, n, 256);
    return model::glob_match({p, pl}, {n, nl});
}

} // namespace

void test_filter_rules() {
    CHECK(glob(L"*", L"anything"));
    CHECK(glob(L"*", L""));
    CHECK(glob(L"@eaDir", L"@EADIR"));
    CHECK(!glob(L"@eaDir", L"@eaDir2"));
    CHECK(glob(L"*.tmp", L"x.TMP"));
    CHECK(!glob(L"*.tmp", L"x.tmp.flac"));
    CHECK(glob(L"a*b*c", L"aXXbYYc"));
    CHECK(!glob(L"a*b*c", L"aXXbYY"));
    CHECK(glob(L"track ?.mp3", L"Track 5.mp3"));
    CHECK(!glob(L"track ?.mp3", L"Track 10.mp3"));
    CHECK(glob(L"**x", L"abx"));
    CHECK(glob(L"\u00C4*", L"\u00E4pfel")); // invariant upper case beyond ASCII

    model::FilterRules rules;
    rules.set_hide_patterns(L" @eaDir ;*.tmp| Thumbs.db ,\r\n;;");
    CHECK(rules.hide_patterns.size() == 3);
    CHECK(rules.hidden_by_pattern(L"thumbs.DB"));
    CHECK(rules.hidden_by_pattern(L"a.tmp"));
    CHECK(!rules.hidden_by_pattern(L"a.flac"));
    CHECK(!rules.empty());

    // Hidden folders: variables expanded, '?' any drive, a trailing backslash dropped, an unset
    // variable drops its entry.
    model::FilterRules paths;
    paths.set_hide_paths({L"%WINDIR%", L"?:\\$Recycle.Bin\\", L"%FILETREE_NOT_SET%\\x"});
    CHECK(paths.hide_paths.size() == 2);
    CHECK(!paths.empty());
    CHECK(paths.hidden_by_path(L"D:\\$RECYCLE.BIN"));
    CHECK(!paths.hidden_by_path(L"D:\\$RECYCLE.BIN\\X"));
    wchar_t windows[MAX_PATH];
    const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
    wchar_t upper[MAX_PATH];
    const std::size_t upper_length = model::to_upper({windows, length}, upper, MAX_PATH);
    CHECK(paths.hidden_by_path({upper, upper_length}));
    CHECK(!paths.hidden_by_path(L"C:\\MUSIC"));
    const auto defaults = settings::default_hidden_folders();
    CHECK(settings::split_paths(settings::join_paths(defaults)) == defaults);
    paths.set_hide_paths(defaults);
    CHECK(paths.hide_paths.size() >= 5); // ProgramFiles(x86) is unset on 32-bit Windows
}

void test_panel_state() {
    using namespace settings;
    PanelState state;
    CHECK(PanelState::decode(state.encode()).empty());
    state.expanded = {L"C:\\", L"C:\\Music", L"D:\\M\u00e9dia\\\u65e5\u672c"};
    state.selected = L"C:\\Music\\a.flac";
    state.top = L"C:\\";
    CHECK(PanelState::decode(state.encode()) == state);
    CHECK(PanelState::decode("").empty());
    CHECK(PanelState::decode("garbage\nE C:\\").empty());
    // Unknown and malformed lines are skipped; CRLF is fine.
    const PanelState read = PanelState::decode("foo_filetree state 1\r\nX future\r\nE\r\nE D:\\x\r\n");
    CHECK(read.expanded.size() == 1 && read.expanded[0] == L"D:\\x" && read.selected.empty());
}

void test_settings_model() {
    using namespace settings;
    test_panel_state();
    const MenuLayout defaults = MenuLayout::defaults();
    CHECK(MenuLayout::decode(defaults.encode()) == defaults);
    CHECK(MenuLayout::decode(L"") == defaults);
    CHECK(MenuLayout::decode(L"garbage,,-,99") == defaults);

    MenuLayout custom = defaults;
    std::swap(custom.order[0], custom.order[5]);
    custom.hidden = 1u << static_cast<unsigned>(MenuItem::copy_path);
    CHECK(custom.encode().find(L"-4") != std::wstring::npos);
    CHECK(MenuLayout::decode(custom.encode()) == custom);
    CHECK(!custom.visible(MenuItem::copy_path) && custom.visible(MenuItem::play));

    // An older config without the last two items: they come back after their predecessors.
    const MenuLayout old = MenuLayout::decode(L"5,0,1,2,3,4,6,7,8");
    CHECK(old.order[0] == MenuItem::rename);
    CHECK(old.order[19] == MenuItem::fb2k_menu && old.order[20] == MenuItem::explorer_menu);
    CHECK(old.order[4] == MenuItem::queue && old.order[5] == MenuItem::save_playlist &&
          old.order[7] == MenuItem::open_with && old.order[8] == MenuItem::properties);
    CHECK(old.order[11] == MenuItem::hide_folder && old.order[12] == MenuItem::new_folder &&
          old.order[13] == MenuItem::cut && old.order[14] == MenuItem::copy &&
          old.order[15] == MenuItem::paste);
    // A newer item goes to its default place (Favourites after Copy path), not to the end.
    CHECK(old.order[9] == MenuItem::copy_path && old.order[10] == MenuItem::favourite);
    CHECK(defaults.order[9] == MenuItem::favourite);
    // Duplicates are dropped and the missing item is restored.
    const MenuLayout dup = MenuLayout::decode(L"0,0,1,2,3,4,5,6,7,8,9,10");
    CHECK(dup == defaults);

    // Favourites: cleaned, de-duplicated, round-tripped.
    CHECK(clean_path(L" \"D:/Media/\" ") == L"D:\\Media");
    CHECK(clean_path(L"E:") == L"E:\\" && clean_path(L"E:\\") == L"E:\\");
    const auto paths = split_paths(L"D:\\Media\\|| C:\\ |d:\\media|\\\\nas\\music\\");
    CHECK(paths.size() == 3 && paths[0] == L"D:\\Media" && paths[1] == L"C:\\" &&
          paths[2] == L"\\\\nas\\music");
    CHECK(split_paths(join_paths(paths)) == paths);
    CHECK(split_paths(L"").empty());

    Settings a;
    Settings b;
    CHECK(diff(a, b) == 0);
    b.favourites = {L"D:\\Media"};
    CHECK(diff(a, b) == change_roots);
    b = a;
    b.favourites_place = FavouritesPlace::after;
    CHECK(diff(a, b) == change_roots);
    b = a;
    b.row_padding = 5;
    CHECK(diff(a, b) == change_remeasure);
    b = a;
    b.hide_patterns = L"x";
    CHECK(diff(a, b) == change_relist);
    b = a;
    b.hidden_folders.pop_back();
    CHECK(diff(a, b) == change_relist);
    b = a;
    b.show_drives = false;
    CHECK(diff(a, b) == change_roots);
    b = a;
    b.lines = TreeLines::guides;
    b.hidden_drives = 1;
    CHECK(diff(a, b) == (change_repaint | change_roots));
    b = a;
    b.sort.reverse = true;
    CHECK(diff(a, b) == change_relist);
    b = a;
    b.follow_playing = true;
    CHECK(diff(a, b) == change_repaint);
    b = a;
    b.read_only = true;
    CHECK(diff(a, b) == change_repaint);
    CHECK(changes_files(MenuItem::remove) && changes_files(MenuItem::explorer_menu));
    CHECK(!changes_files(MenuItem::copy) && !changes_files(MenuItem::play));
    b = a;
    b.zebra = true;
    b.tooltips = Tooltips::path;
    CHECK(diff(a, b) == change_repaint);
    b = a;
    b.hover_highlight = false;
    CHECK(diff(a, b) == change_repaint);
    b = a;
    b.show_status_bar = true;
    CHECK(diff(a, b) == change_layout);
    b = a;
    b.status_counters = true;
    CHECK(diff(a, b) == change_layout);
    b = a;
    b.transparent = true;
    CHECK(diff(a, b) == (change_layout | change_repaint));

    Settings wild;
    wild.line_thickness = 99;
    wild.row_padding = -3;
    wild.line_opacity = 0;
    wild.lines = static_cast<TreeLines>(7);
    wild.files = static_cast<fs::FileMode>(9);
    wild.hidden_drives = 0xffffffffu;
    wild.tooltips = static_cast<Tooltips>(5);
    wild.sanitize();
    CHECK(wild.tooltips == Tooltips::off);
    CHECK(wild.line_thickness == 4 && wild.row_padding == 0 && wild.line_opacity == 10);
    CHECK(wild.lines == TreeLines::none && wild.files == fs::FileMode::all);
    CHECK(wild.hidden_drives == (1u << 26) - 1);
}

void test_presets() {
    using namespace actions;
    const Bindings defaults = Bindings::defaults();
    // Every default action has a preset (else the editor would show None and lose it).
    for (const Binding& binding : defaults.gestures) {
        CHECK(binding.folder.kind == Kind::none || preset_index(binding.folder, true) != 0);
        CHECK(binding.file.kind == Kind::none || preset_index(binding.file, false) != 0);
    }
    CHECK(presets(true).size() == presets(false).size() + 1);
    CHECK(preset_index(Action{Kind::toggle}, false) == 0); // files cannot expand
    // Recursion survives a round trip through the editor.
    Action action = presets(false)[3].action;
    action.recursion = Recursion::never;
    const std::size_t index = preset_index(action, false);
    CHECK(index == 3);
    CHECK(from_preset(index, false, action) == action);
    CHECK(from_preset(99, true, action).kind == Kind::none);
}

void test_enumerate_rules(const std::filesystem::path& base) {
    std::atomic<bool> cancel{false};
    auto playable = std::make_shared<model::ExtensionSet>();
    playable->add_mask(L"*.flac;*.mp3");
    auto rules = std::make_shared<model::FilterRules>();
    rules->always_show.add_mask(L"jpg");
    rules->never_show.add_mask(L"flac");
    rules->set_hide_patterns(L"Album 1*");

    fs::EnumOptions options;
    options.playable = playable;
    options.rules = rules;
    const auto listing = fs::enumerate_folder(base.wstring(), options, cancel);
    std::vector<std::wstring> names;
    for (const auto& item : listing.items) names.emplace_back(listing.name(item));
    // Album 10 hidden by pattern, flac never shown, jpg always shown, hidden.mp3 hidden.
    const std::vector<std::wstring> expected{L"Album 2", L"big", L"cover.jpg", L"Track 2.MP3"};
    CHECK(names == expected);

    options.files = fs::FileMode::none;
    const auto folders = fs::enumerate_folder(base.wstring(), options, cancel);
    CHECK(folders.items.size() == 2); // always_show does not apply to "folders only"

    // A folder hidden by its full path (any case); files of the same name stay.
    auto by_path = std::make_shared<model::FilterRules>();
    by_path->set_hide_paths({base.wstring() + L"\\album 2\\"});
    options.rules = by_path;
    const auto without = fs::enumerate_folder(base.wstring(), options, cancel);
    names.clear();
    for (const auto& item : without.items) names.emplace_back(without.name(item));
    const std::vector<std::wstring> rest{L"Album 10", L"big"};
    CHECK(names == rest);
    std::wstring slash = base.wstring() + L"\\";
    CHECK(fs::enumerate_folder(slash, options, cancel).items.size() == 2);
}

// The watcher posts to a window: a message-only one here, pumped by hand.
void test_watcher(const std::filesystem::path& base) {
    const std::filesystem::path folder = base / L"watched";
    std::filesystem::create_directories(folder);
    HWND wnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr,
                               nullptr);
    CHECK(wnd != nullptr);
    constexpr UINT message = WM_APP + 1;
    const auto wait_for = [&](fs::WatchId id, DWORD ms) {
        const DWORD start = GetTickCount();
        MSG msg{};
        while (GetTickCount() - start < ms) {
            while (PeekMessageW(&msg, wnd, message, message, PM_REMOVE)) {
                if (static_cast<fs::WatchId>(msg.lParam) == id) return true;
            }
            Sleep(10);
        }
        return false;
    };
    const fs::WatchId id = fs::watch_folder(folder.wstring(), wnd, message);
    CHECK(id != 0);
    Sleep(200); // the thread opens the folder
    HANDLE file = CreateFileW((folder / L"new.txt").c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    CloseHandle(file);
    CHECK(wait_for(id, 2000));
    // After unwatch, nothing more arrives and the watch is freed.
    fs::unwatch_folder(id);
    Sleep(100);
    CHECK(fs::watch_count() == 0);
    std::filesystem::remove(folder / L"new.txt");
    CHECK(!wait_for(id, 300));
    // A folder that does not exist ends by itself; unwatching it later is harmless.
    const fs::WatchId missing = fs::watch_folder((base / L"nope").wstring(), wnd, message);
    Sleep(200);
    CHECK(fs::watch_count() == 0);
    fs::unwatch_folder(missing);
    // A deleted watched folder reports once and ends.
    const std::filesystem::path doomed = base / L"doomed";
    std::filesystem::create_directories(doomed);
    const fs::WatchId gone = fs::watch_folder(doomed.wstring(), wnd, message);
    Sleep(200);
    std::filesystem::remove(doomed);
    CHECK(wait_for(gone, 2000));
    fs::shutdown_watcher();
    CHECK(fs::watch_folder(folder.wstring(), wnd, message) == 0);
    DestroyWindow(wnd);
}
