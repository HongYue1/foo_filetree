#include "file_kind.h"

namespace filetree::model {
namespace {

struct Entry {
    const wchar_t* extension; //!< lower case
    FileKind kind;
};

// Playlist types come first in the caller's eyes: foobar2000 can open them, but they are lists,
// not audio. Video containers foobar2000 may also play still get the video icon.
constexpr Entry entries[] = {
    {L"m3u", FileKind::playlist},  {L"m3u8", FileKind::playlist}, {L"pls", FileKind::playlist},
    {L"fpl", FileKind::playlist},  {L"xspf", FileKind::playlist}, {L"asx", FileKind::playlist},
    {L"wpl", FileKind::playlist},  {L"cue", FileKind::playlist},
    {L"mp4", FileKind::video},     {L"m4v", FileKind::video},     {L"mkv", FileKind::video},
    {L"webm", FileKind::video},    {L"avi", FileKind::video},     {L"mov", FileKind::video},
    {L"wmv", FileKind::video},     {L"flv", FileKind::video},     {L"mpg", FileKind::video},
    {L"mpeg", FileKind::video},    {L"m2ts", FileKind::video},    {L"mts", FileKind::video},
    {L"ts", FileKind::video},      {L"vob", FileKind::video},     {L"3gp", FileKind::video},
    {L"ogv", FileKind::video},
    {L"jpg", FileKind::image},     {L"jpeg", FileKind::image},    {L"png", FileKind::image},
    {L"gif", FileKind::image},     {L"bmp", FileKind::image},     {L"webp", FileKind::image},
    {L"tif", FileKind::image},     {L"tiff", FileKind::image},    {L"avif", FileKind::image},
    {L"heic", FileKind::image},    {L"jxl", FileKind::image},     {L"ico", FileKind::image},
    {L"txt", FileKind::text},      {L"log", FileKind::text},      {L"nfo", FileKind::text},
    {L"md", FileKind::text},       {L"ini", FileKind::text},      {L"json", FileKind::text},
    {L"xml", FileKind::text},      {L"csv", FileKind::text},      {L"sfv", FileKind::text},
    {L"md5", FileKind::text},      {L"ffp", FileKind::text},      {L"accurip", FileKind::text},
    {L"pdf", FileKind::pdf},
};

[[nodiscard]] bool equals_lower(std::wstring_view text, const wchar_t* lower) noexcept {
    std::size_t i = 0;
    for (; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        if (lower[i] == L'\0' || c != lower[i]) return false;
    }
    return lower[i] == L'\0';
}

} // namespace

FileKind file_kind(std::wstring_view extension) noexcept {
    if (extension.empty() || extension.size() > 7) return FileKind::other;
    for (const Entry& entry : entries) {
        if (equals_lower(extension, entry.extension)) return entry.kind;
    }
    return FileKind::other;
}

} // namespace filetree::model
