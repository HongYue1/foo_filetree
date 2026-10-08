// Offline tests for model/filter_rules and settings/settings_model.

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "../src/fs/enumerate.h"
#include "../src/model/filter_rules.h"
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
}

void test_settings_model() {
    using namespace settings;
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
    CHECK(old.order[9] == MenuItem::fb2k_menu && old.order[10] == MenuItem::explorer_menu);
    // Duplicates are dropped and the missing item is restored.
    const MenuLayout dup = MenuLayout::decode(L"0,0,1,2,3,4,5,6,7,8,9,10");
    CHECK(dup == defaults);

    Settings a;
    Settings b;
    CHECK(diff(a, b) == 0);
    b.row_padding = 5;
    CHECK(diff(a, b) == change_remeasure);
    b = a;
    b.hide_patterns = L"x";
    CHECK(diff(a, b) == change_relist);
    b = a;
    b.lines = TreeLines::guides;
    b.hidden_drives = 1;
    CHECK(diff(a, b) == (change_repaint | change_roots));
    b = a;
    b.sort.reverse = true;
    CHECK(diff(a, b) == change_relist);

    Settings wild;
    wild.line_thickness = 99;
    wild.row_padding = -3;
    wild.line_opacity = 0;
    wild.lines = static_cast<TreeLines>(7);
    wild.files = static_cast<fs::FileMode>(9);
    wild.hidden_drives = 0xffffffffu;
    wild.sanitize();
    CHECK(wild.line_thickness == 4 && wild.row_padding == 0 && wild.line_opacity == 10);
    CHECK(wild.lines == TreeLines::none && wild.files == fs::FileMode::all);
    CHECK(wild.hidden_drives == (1u << 26) - 1);
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
}
