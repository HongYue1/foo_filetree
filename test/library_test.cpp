// Library folders derived from track paths, and the "holds tracks" folder set.

#include "check.h"

#include "../src/model/library_index.h"

using filetree::model::LibraryIndex;

void test_library_index() {
    LibraryIndex index;
    CHECK(index.add(L"C:\\Music\\Rock\\Album\\01.flac", L"Rock\\Album\\01.flac"));
    CHECK(index.add(L"C:\\Music\\Rock\\Album\\02.flac", L"Rock\\Album\\02.flac"));
    CHECK(index.add(L"C:\\Music\\top.mp3", L"top.mp3"));
    CHECK(index.add(L"D:\\x\\y.mp3", L"x\\y.mp3"));     // a whole drive in the library
    CHECK(index.add(L"D:\\root.mp3", L"root.mp3"));
    CHECK(index.add(L"\\\\nas\\share\\Jazz\\a.mp3", L"Jazz\\a.mp3"));
    CHECK(index.add(L"c:\\music\\Pop\\b.mp3", L"Pop\\b.mp3")); // same root, other case
    CHECK(!index.add(L"C:\\Other\\a.mp3", L"Elsewhere\\a.mp3")); // does not fit
    CHECK(!index.add(L"C:\\a.mp3", L""));
    index.finish();

    const std::vector<std::wstring> roots{L"C:\\Music", L"D:\\", L"\\\\nas\\share"};
    CHECK(index.roots() == roots);
    CHECK(index.holds_tracks(L"C:\\MUSIC"));
    CHECK(index.holds_tracks(L"C:\\MUSIC\\ROCK"));
    CHECK(index.holds_tracks(L"C:\\MUSIC\\ROCK\\ALBUM"));
    CHECK(index.holds_tracks(L"C:\\MUSIC\\POP"));
    CHECK(index.holds_tracks(L"D:\\X"));
    CHECK(index.holds_tracks(L"\\\\NAS\\SHARE\\JAZZ"));
    CHECK(!index.holds_tracks(L"C:\\MUSIC\\ROCK\\ALBUM\\01.FLAC"));
    CHECK(!index.holds_tracks(L"C:\\OTHER"));
    CHECK(!index.holds_tracks(L"C:"));

    LibraryIndex same;
    same.add(L"\\\\nas\\share\\z.mp3", L"z.mp3");
    same.add(L"D:\\z.mp3", L"z.mp3");
    same.add(L"C:\\MUSIC\\z.mp3", L"z.mp3");
    same.finish();
    CHECK(same.same_roots(index));

    // Roots remembered from the last session match the built index: no relist when it comes in.
    LibraryIndex remembered;
    remembered.add_root(L"D:\\");
    remembered.add_root(L"c:\\music");
    remembered.add_root(L"\\\\nas\\share");
    remembered.add_root(L"C:\\Music");
    remembered.add_root(L"");
    remembered.finish();
    CHECK(remembered.roots().size() == 3 && remembered.same_roots(index));
    CHECK(remembered.folder_count() == 0);
}
