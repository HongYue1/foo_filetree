// File kinds for icons: extension lookups, case-insensitive, no false matches.

#include "check.h"

#include "../src/model/file_kind.h"

using filetree::model::FileKind;
using filetree::model::file_kind;

void test_file_kinds() {
    CHECK(file_kind(L"mkv") == FileKind::video);
    CHECK(file_kind(L"MP4") == FileKind::video);
    CHECK(file_kind(L"Jpg") == FileKind::image);
    CHECK(file_kind(L"txt") == FileKind::text);
    CHECK(file_kind(L"LOG") == FileKind::text);
    CHECK(file_kind(L"pdf") == FileKind::pdf);
    CHECK(file_kind(L"cue") == FileKind::playlist);
    CHECK(file_kind(L"m3u8") == FileKind::playlist);
    CHECK(file_kind(L"flac") == FileKind::other);
    CHECK(file_kind(L"") == FileKind::other);
    CHECK(file_kind(L"m3") == FileKind::other);     // a prefix of m3u
    CHECK(file_kind(L"m3uu") == FileKind::other);   // longer than m3u
    CHECK(file_kind(L"accurip") == FileKind::text);
    CHECK(file_kind(L"averylongext") == FileKind::other);
}
