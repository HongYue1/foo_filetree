// Offline tests for model/, fs/enumerate, fs/enumeration_service and platform/worker_pool.
// No foobar2000 needed. Build and run: test\build_tests.bat (output in test\tests.out).

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "check.h"
#include "../src/actions/action.h"
#include "../src/fs/drives.h"
#include "../src/fs/enumerate.h"
#include "../src/fs/enumeration_service.h"
#include "../src/model/extension_set.h"
#include "../src/model/sort.h"
#include "../src/model/tree.h"

using namespace filetree;

// settings_test.cpp
void test_filter_rules();
void test_settings_model();
void test_presets();
void test_enumerate_rules(const std::filesystem::path& base);
void test_watcher(const std::filesystem::path& base);
// selection_test.cpp
void test_selection();
void test_status();
void test_accessible();

namespace {


using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

model::SortItem file(const wchar_t* name, std::uint64_t size = 0, std::int64_t modified = 0) {
    return {name, FILE_ATTRIBUTE_ARCHIVE, size, modified};
}
model::SortItem folder(const wchar_t* name) { return {name, FILE_ATTRIBUTE_DIRECTORY, 0, 0}; }

void test_sort() {
    const model::SortOptions natural{};
    CHECK(model::compare_natural(L"Track 2", L"Track 10") < 0);
    CHECK(model::compare_natural(L"track 2", L"Track 2") != 0); // total order: ordinal tie-break
    CHECK(model::compare_natural(L"abc", L"ABD") < 0);         // case-insensitive first
    CHECK(model::compare_name(L"Track 2", L"Track 10") > 0);   // plain name: '2' > '1'

    CHECK(model::sort_less(folder(L"zzz"), file(L"aaa"), natural));
    CHECK(!model::sort_less(file(L"aaa"), folder(L"zzz"), natural));
    CHECK(model::sort_less(file(L"01 Intro.flac"), file(L"2 Song.flac"), natural));

    model::SortOptions mixed{};
    mixed.folders_first = false;
    CHECK(model::sort_less(file(L"aaa"), folder(L"zzz"), mixed));

    model::SortOptions reverse{};
    reverse.reverse = true;
    CHECK(model::sort_less(folder(L"b"), folder(L"a"), reverse));
    CHECK(model::sort_less(folder(L"a"), file(L"z"), reverse)); // folders still first

    model::SortOptions by_size{model::SortField::size};
    CHECK(model::sort_less(file(L"z", 1), file(L"a", 2), by_size));
    CHECK(model::sort_less(file(L"a", 5), file(L"b", 5), by_size));

    model::SortOptions by_type{model::SortField::type};
    CHECK(model::sort_less(file(L"z.flac"), file(L"a.mp3"), by_type));
    CHECK(model::sort_less(file(L"noext"), file(L"a.mp3"), by_type)); // empty extension first

    model::SortOptions by_date{model::SortField::modified};
    CHECK(model::sort_less(file(L"z", 0, 1), file(L"a", 0, 2), by_date));

    CHECK(model::extension_of(L"a.tar.gz") == L"gz");
    CHECK(model::extension_of(L".nomedia").empty());
    CHECK(model::extension_of(L"name.").empty());
    CHECK(model::extension_of(L"noext").empty());
}

void test_extensions() {
    model::ExtensionSet set;
    set.add_mask(L"*.MP3;*.MP2");
    set.add_mask(L" flac , .ogg | *.Opus");
    set.add(L"*");   // wildcard only: ignored
    set.add(L"*.*"); // ignored
    set.add(L"averyveryveryverylongextensionthatexceedsthelimit");
    CHECK(set.size() == 5);
    CHECK(set.contains(L"mp3"));
    CHECK(set.contains(L"MP2"));
    CHECK(set.contains(L"Opus"));
    CHECK(set.matches_file(L"Song.FLAC"));
    CHECK(set.matches_file(L"x.ogg"));
    CHECK(!set.matches_file(L"cover.jpg"));
    CHECK(!set.matches_file(L"mp3"));
    CHECK(!set.matches_file(L".mp3"));
}

std::vector<model::ChildRecord> records(std::initializer_list<const wchar_t*> folders,
                                        std::initializer_list<const wchar_t*> files) {
    std::vector<model::ChildRecord> out;
    for (const wchar_t* name : folders) out.push_back({name, FILE_ATTRIBUTE_DIRECTORY, 0, 0});
    for (const wchar_t* name : files) out.push_back({name, FILE_ATTRIBUTE_ARCHIVE, 1, 0});
    return out;
}

std::wstring row_name(const model::Tree& tree, std::size_t row) {
    return std::wstring(tree.node(tree.node_at_row(row)).name_view());
}

void test_tree() {
    using model::Tree;
    Tree tree;
    const auto c = tree.add_root(L"C:\\");
    const auto d = tree.add_root(L"D:\\");
    CHECK(tree.row_count() == 2);

    CHECK(tree.expand(c) == Tree::ExpandResult::needs_load);
    CHECK(tree.expand(c) == Tree::ExpandResult::unchanged);
    auto splice = tree.apply_children(c, records({L"Music", L"Users"}, {L"a.mp3"}));
    CHECK(splice.row == 1 && splice.inserted == 3 && splice.removed == 0);
    CHECK(tree.row_count() == 5);
    CHECK(row_name(tree, 1) == L"Music");
    CHECK(row_name(tree, 4) == L"D:\\");

    const auto music = tree.node_at_row(1);
    std::wstring path;
    tree.build_path(music, path);
    CHECK(path == L"C:\\Music");
    CHECK(tree.expand(tree.node_at_row(3)) == Tree::ExpandResult::not_container);

    CHECK(tree.expand(music) == Tree::ExpandResult::needs_load);
    splice = tree.apply_children(music, records({L"Album"}, {L"x.flac"}));
    CHECK(splice.row == 2 && splice.inserted == 2);
    CHECK(tree.row_count() == 7);
    const auto album = tree.node_at_row(2);
    tree.build_path(album, path);
    CHECK(path == L"C:\\Music\\Album");
    CHECK(tree.node(album).depth == 2);

    // A listing nobody asked for is ignored.
    CHECK(tree.apply_children(album, records({}, {L"y.mp3"})).empty());

    // Collapse the root: all five descendants go.
    splice = tree.collapse(c);
    CHECK(splice.row == 1 && splice.removed == 5);
    CHECK(tree.row_count() == 2);
    CHECK(tree.collapse(c).empty());

    // Re-expand: cached, and Music comes back expanded.
    model::RowSplice again;
    CHECK(tree.expand(c, &again) == Tree::ExpandResult::expanded);
    CHECK(again.row == 1 && again.inserted == 5);
    CHECK(row_name(tree, 2) == L"Album");
    const std::size_t nodes_before = tree.node_count();
    CHECK(nodes_before == 7);

    // Collapse while loading: the listing applies but adds no rows; expanding shows it.
    CHECK(tree.expand(d) == Tree::ExpandResult::needs_load);
    tree.collapse(d);
    CHECK(tree.apply_children(d, records({L"Backup"}, {})).empty());
    CHECK(tree.node(d).has(model::node_loaded));
    CHECK(tree.expand(d, &again) == Tree::ExpandResult::expanded && again.inserted == 1);

    // A failed load can be retried.
    const auto users = tree.node_at_row(*tree.row_of(music) + 3);
    CHECK(tree.node(users).name_view() == L"Users");
    CHECK(tree.expand(users) == Tree::ExpandResult::needs_load);
    tree.fail_load(users);
    CHECK(tree.node(users).has(model::node_load_failed));
    CHECK(tree.expand(users) == Tree::ExpandResult::needs_load);
    CHECK(!tree.node(users).has(model::node_load_failed));

    static_assert(sizeof(model::Node) <= 64);
    std::printf("sizeof(Node) = %zu bytes\n", sizeof(model::Node));
}

void test_tree_reload() {
    using model::Tree;
    Tree tree;
    const auto c = tree.add_root(L"C:\\");
    const auto d = tree.add_root(L"D:\\");
    tree.expand(c);
    tree.apply_children(c, records({L"Music", L"Users"}, {L"a.mp3"}));
    const auto music = tree.node_at_row(1);
    const auto users = tree.node_at_row(2);
    tree.expand(music);
    tree.apply_children(music, records({L"Album"}, {L"x.flac"}));
    CHECK(tree.row_count() == 7);
    CHECK(tree.find_child(c, L"users") == users);
    CHECK(tree.find_child(c, L"nope") == model::no_node);
    CHECK(tree.find_child(d, L"x") == model::no_node);

    // Users is loading when the root reloads: its listing must then be ignored.
    CHECK(tree.expand(users) == Tree::ExpandResult::needs_load);
    auto result = tree.reload(c);
    CHECK(result.needs_load);
    CHECK(result.splice.row == 1 && result.splice.removed == 5 && result.splice.inserted == 0);
    CHECK(tree.row_count() == 2);
    CHECK(tree.node(c).has(model::node_loading) && !tree.node(c).has(model::node_loaded));
    CHECK(tree.apply_children(users, records({L"Bob"}, {})).empty());
    CHECK(tree.row_count() == 2);
    // Reloading while loading is a no-op.
    CHECK(!tree.reload(c).needs_load);

    auto splice = tree.apply_children(c, records({L"Music", L"Renamed"}, {}));
    CHECK(splice.row == 1 && splice.inserted == 2);
    CHECK(tree.find_child(c, L"Renamed") == tree.node_at_row(2));
    // Music came back as a fresh, collapsed node.
    CHECK(!tree.node(tree.node_at_row(1)).has(model::node_expanded));
    CHECK(row_name(tree, 3) == L"D:\\");

    // A collapsed folder: forgets its children, no listing, re-expand lists again.
    tree.expand(d);
    tree.apply_children(d, records({L"Backup"}, {}));
    tree.collapse(d);
    result = tree.reload(d);
    CHECK(!result.needs_load && result.splice.empty());
    CHECK(!tree.node(d).has(model::node_loaded));
    CHECK(tree.expand(d) == Tree::ExpandResult::needs_load);
}

void test_tree_merge() {
    using model::Tree;
    Tree tree;
    const auto c = tree.add_root(L"C:\\");
    tree.add_root(L"D:\\");
    tree.expand(c);
    tree.apply_children(c, records({L"Music", L"Old", L"Users"}, {L"a.mp3"}));
    const auto music = tree.find_child(c, L"Music");
    tree.expand(music);
    tree.apply_children(music, records({L"Album"}, {L"x.flac"}));
    const auto album = tree.find_child(music, L"Album");
    CHECK(tree.row_count() == 8); // C, Music, Album, x.flac, Old, Users, a.mp3, D

    CHECK(tree.children_match(c, records({L"Music", L"Old", L"Users"}, {L"a.mp3"})));
    CHECK(!tree.children_match(c, records({L"Music", L"Users"}, {L"a.mp3"})));
    CHECK(!tree.children_match(c, records({L"Music", L"Old", L"users"}, {L"a.mp3"})));
    CHECK(!tree.children_match(music, records({L"Album"}, {})) );
    CHECK(!tree.children_match(album, {}));                    // not loaded

    // Old is gone, New appears, a.mp3 grew: Music keeps its open subtree in one splice.
    auto fresh = records({L"Music", L"New", L"Users"}, {L"a.mp3"});
    fresh[3].size = 99;
    auto result = tree.merge_children(c, fresh);
    CHECK(result.splice.row == 1 && result.splice.removed == 6 && result.splice.inserted == 6);
    CHECK(tree.row_count() == 8);
    const auto music2 = result.map(music);
    CHECK(music2 != music && music2 == tree.find_child(c, L"Music"));
    CHECK(tree.node(music2).has(model::node_expanded) && tree.node(music2).has(model::node_loaded));
    CHECK(tree.node(album).parent == music2);
    CHECK(result.map(album) == album);           // deeper nodes keep their index
    CHECK(row_name(tree, 2) == L"Album" && row_name(tree, 4) == L"New");
    CHECK(tree.node(tree.find_child(c, L"a.mp3")).size == 99);
    CHECK(tree.find_child(c, L"Old") == model::no_node);
    CHECK(tree.children_match(c, fresh));
    std::wstring path;
    tree.build_path(album, path);
    CHECK(path == L"C:\\Music\\Album");

    // A folder that became a file is a new node; collapsed folders splice nothing.
    tree.collapse(c);
    result = tree.merge_children(c, records({L"New", L"Users"}, {L"Music"}));
    CHECK(result.splice.empty());
    CHECK(!tree.node(tree.find_child(c, L"Music")).has(model::node_container));
    CHECK(result.map(music2) == model::no_node);
    // Loading or unloaded folders are left alone.
    CHECK(tree.expand(tree.find_child(c, L"Users")) == Tree::ExpandResult::needs_load);
    CHECK(tree.merge_children(tree.find_child(c, L"Users"), {}).moved.empty());
}

void test_tree_filter() {
    using model::Tree;
    Tree tree;
    const auto c = tree.add_root(L"C:\\");
    tree.add_root(L"D:\\");
    tree.expand(c);
    tree.apply_children(c, records({L"Music", L"Users"}, {L"a.mp3"}));
    const auto music = tree.find_child(c, L"Music");
    tree.expand(music);
    tree.apply_children(music, records({L"Album"}, {L"x.flac", L"song.mp3"}));
    CHECK(tree.row_count() == 8); // C, Music, Album, x.flac, song.mp3, Users, a.mp3, D

    // "mp3": matches and their ancestors only.
    auto splice = tree.set_filter(L"Mp3");
    CHECK(splice.full && splice.removed == 8);
    CHECK(tree.filtered());
    CHECK(tree.row_count() == 4); // C, Music, song.mp3, a.mp3
    CHECK(row_name(tree, 2) == L"song.mp3");
    CHECK(row_name(tree, 3) == L"a.mp3");
    CHECK(tree.previous_node_at(7) == 1); // D was the last row
    CHECK(tree.set_filter(L"MP3").empty()); // same text, nothing to do

    // A matching folder keeps its (expanded) contents.
    tree.set_filter(L"music");
    CHECK(tree.row_count() == 5); // C, Music, Album, x.flac, song.mp3

    // Wildcards match whole names.
    tree.set_filter(L"*.flac");
    CHECK(tree.row_count() == 3 && row_name(tree, 2) == L"x.flac");

    // Row changes while filtered rebuild everything.
    splice = tree.collapse(music);
    CHECK(splice.full && tree.row_count() == 0);
    tree.expand(music);
    CHECK(tree.row_count() == 3);

    // Clearing shows every expanded row again.
    splice = tree.set_filter(L"");
    CHECK(splice.full && !tree.filtered() && tree.row_count() == 8);
}

void test_tree_large() {
    model::Tree tree;
    const auto root = tree.add_root(L"C:\\");
    std::vector<std::wstring> names;
    names.reserve(10000);
    for (int i = 0; i < 10000; ++i) names.push_back(L"Track " + std::to_wstring(i) + L".flac");
    std::vector<model::ChildRecord> children;
    for (const auto& name : names) children.push_back({name, FILE_ATTRIBUTE_ARCHIVE, 1, 0});

    tree.expand(root);
    auto start = Clock::now();
    const auto splice = tree.apply_children(root, children);
    const double apply_ms = ms_since(start);
    CHECK(splice.inserted == 10000);

    start = Clock::now();
    tree.collapse(root);
    model::RowSplice re;
    tree.expand(root, &re);
    const double toggle_ms = ms_since(start);
    CHECK(re.inserted == 10000);
    std::printf("10k children: apply %.3f ms, collapse+expand %.3f ms, memory %zu KB\n", apply_ms,
                toggle_ms, tree.memory_bytes() / 1024);
}

void test_search_pattern() {
    CHECK(fs::make_search_pattern(L"C:\\") == L"C:\\*");
    CHECK(fs::make_search_pattern(L"C:\\Music") == L"C:\\Music\\*");
    const std::wstring long_path = L"C:\\" + std::wstring(300, L'a');
    CHECK(fs::make_search_pattern(long_path) == L"\\\\?\\" + long_path + L"\\*");
    const std::wstring long_unc = L"\\\\server\\share\\" + std::wstring(300, L'b');
    CHECK(fs::make_search_pattern(long_unc) ==
          L"\\\\?\\UNC\\server\\share\\" + std::wstring(300, L'b') + L"\\*");
    CHECK(fs::make_search_pattern(L"\\\\?\\C:\\x") == L"\\\\?\\C:\\x\\*");
}

void touch(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}

std::filesystem::path make_fixture() {
    namespace stdfs = std::filesystem;
    const stdfs::path base = stdfs::temp_directory_path() / L"foo_filetree_test";
    std::error_code ignored;
    stdfs::remove_all(base, ignored);
    stdfs::create_directories(base / L"Album 10");
    stdfs::create_directories(base / L"Album 2");
    touch(base / L"Track 10.flac");
    touch(base / L"Track 2.MP3");
    touch(base / L"cover.jpg");
    touch(base / L"hidden.mp3");
    SetFileAttributesW((base / L"hidden.mp3").c_str(), FILE_ATTRIBUTE_HIDDEN);
    stdfs::create_directories(base / L"big");
    for (int i = 0; i < 10000; ++i) touch(base / L"big" / (L"f" + std::to_wstring(i) + L".mp3"));
    return base;
}

std::vector<std::wstring> names_of(const fs::Listing& listing) {
    std::vector<std::wstring> out;
    for (const auto& item : listing.items) out.emplace_back(listing.name(item));
    return out;
}

void test_enumerate(const std::filesystem::path& base) {
    auto playable = std::make_shared<model::ExtensionSet>();
    playable->add_mask(L"*.flac;*.mp3");
    std::atomic<bool> cancel{false};

    fs::EnumOptions options;
    options.playable = playable;
    auto listing = fs::enumerate_folder(base.wstring(), options, cancel);
    CHECK(listing.error == ERROR_SUCCESS);
    const std::vector<std::wstring> expected{L"Album 2", L"Album 10", L"big", L"Track 2.MP3",
                                             L"Track 10.flac"};
    CHECK(names_of(listing) == expected);

    options.files = fs::FileMode::all;
    options.show_hidden = true;
    listing = fs::enumerate_folder(base.wstring(), options, cancel);
    CHECK(listing.items.size() == 7);

    options.files = fs::FileMode::none;
    listing = fs::enumerate_folder(base.wstring(), options, cancel);
    CHECK(listing.items.size() == 3);

    listing = fs::enumerate_folder((base / L"missing").wstring(), options, cancel);
    CHECK(listing.error == ERROR_PATH_NOT_FOUND);

    cancel = true;
    listing = fs::enumerate_folder((base / L"big").wstring(), options, cancel);
    CHECK(listing.cancelled && listing.items.empty());
    cancel = false;

    options.files = fs::FileMode::playable;
    const auto start = Clock::now();
    listing = fs::enumerate_folder((base / L"big").wstring(), options, cancel);
    const double ms = ms_since(start);
    CHECK(listing.items.size() == 10000);
    CHECK(listing.name(listing.items[2]) == L"f2.mp3");
    std::printf("enumerate+filter+sort 10k files (warm cache): %.2f ms on a worker\n", ms);

    std::vector<model::ChildRecord> records;
    listing.to_records(records);
    CHECK(records.size() == 10000 && records[0].name == L"f0.mp3");
}

// A main-thread stand-in: workers post here, the test pumps.
struct MainQueue {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::function<void()>> queue;

    void post(std::function<void()> work) {
        {
            std::lock_guard lock(mutex);
            queue.push_back(std::move(work));
        }
        cv.notify_one();
    }

    //! Runs posted work until `done` or the timeout. Returns `done()`.
    bool pump_until(const std::function<bool()>& done, int timeout_ms) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        while (!done()) {
            std::unique_lock lock(mutex);
            if (!cv.wait_until(lock, deadline, [&] { return !queue.empty(); })) return done();
            auto work = std::move(queue.front());
            queue.pop_front();
            lock.unlock();
            work();
        }
        return true;
    }
};

void test_service(const std::filesystem::path& base) {
    MainQueue main;
    fs::EnumerationService service([&](std::function<void()> work) { main.post(std::move(work)); },
                                   2);
    const DWORD main_thread = GetCurrentThreadId();

    model::Tree tree;
    const auto root = tree.add_root(base.wstring());
    CHECK(tree.expand(root) == model::Tree::ExpandResult::needs_load);

    bool done = false;
    bool on_main = false;
    std::wstring path;
    tree.build_path(root, path);
    fs::EnumOptions options;
    options.files = fs::FileMode::all;
    service.request(path, options, [&](fs::Listing& listing) {
        on_main = GetCurrentThreadId() == main_thread;
        std::vector<model::ChildRecord> records;
        listing.to_records(records);
        tree.apply_children(root, records);
        done = true;
    });
    CHECK(main.pump_until([&] { return done; }, 5000));
    CHECK(on_main);
    CHECK(tree.row_count() == 1 + 6);

    // Cancelled before it runs: the callback never fires.
    bool cancelled_fired = false;
    const auto ticket = service.request((base / L"big").wstring(), options,
                                        [&](fs::Listing&) { cancelled_fired = true; });
    ticket.cancel();
    bool sentinel = false;
    service.request(base.wstring(), options, [&](fs::Listing&) { sentinel = true; });
    CHECK(main.pump_until([&] { return sentinel; }, 5000));
    main.pump_until([] { return false; }, 100);
    CHECK(!cancelled_fired);

    service.shutdown(2000);
    CHECK(service.pending() == 0);
}

void test_drives() {
    const auto roots = fs::drive_roots();
    CHECK(!roots.empty());
    CHECK(!roots.empty() && roots[0].size() == 3 && roots[0][1] == L':' && roots[0][2] == L'\\');
}

void test_actions() {
    using namespace filetree::actions;
    const Bindings defaults = Bindings::defaults();
    CHECK(defaults.lookup(Gesture::single_click, true).kind == Kind::none);
    CHECK(defaults.lookup(Gesture::single_click, false).kind == Kind::none);
    CHECK(defaults.lookup(Gesture::double_click, true).kind == Kind::toggle);
    CHECK(defaults.lookup(Gesture::double_click, false).play);
    CHECK(defaults.lookup(Gesture::middle_click, false).target == Target::active);

    Bindings custom = defaults;
    custom.gestures[0].file = {Kind::send, Target::new_playlist, Mode::add, true, Recursion::never};
    std::uint8_t blob[encoded_bindings_size];
    const std::size_t size = encode(custom, blob);
    CHECK(size == encoded_bindings_size);
    CHECK(decode(blob, size) == custom);

    CHECK(decode(nullptr, 0) == defaults);
    CHECK(decode(blob, 3) == defaults);
    // Truncated after the first gesture: the rest are defaults.
    const Bindings truncated = decode(blob, 4 + 10);
    CHECK(truncated.gestures[0] == custom.gestures[0]);
    CHECK(truncated.gestures[1] == defaults.gestures[1]);
    // A bad byte in one action resets only that action.
    std::uint8_t bad[encoded_bindings_size];
    std::copy(std::begin(blob), std::end(blob), bad);
    bad[4 + 5] = 99; // gesture 0, file action, kind
    const Bindings repaired = decode(bad, size);
    CHECK(repaired.gestures[0].file == defaults.gestures[0].file);
    CHECK(repaired.gestures[0].folder == custom.gestures[0].folder);
    // A foreign blob is ignored.
    bad[0] = 'X';
    CHECK(decode(bad, size) == defaults);
}

} // namespace

int main() {
    test_actions();
    test_filter_rules();
    test_settings_model();
    test_presets();
    test_sort();
    test_extensions();
    test_tree();
    test_tree_reload();
    test_tree_merge();
    test_tree_filter();
    test_selection();
    test_status();
    test_accessible();
    test_tree_large();
    test_search_pattern();
    const auto base = make_fixture();
    test_enumerate(base);
    test_service(base);
    test_enumerate_rules(base);
    test_watcher(base);
    test_drives();
    std::error_code ignored;
    std::filesystem::remove_all(base, ignored);

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

