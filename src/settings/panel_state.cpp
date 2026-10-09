#include "panel_state.h"

#include <windows.h>

namespace filetree::settings {
namespace {

constexpr std::string_view header = "foo_filetree state 1";

void append_utf8(std::string& out, std::wstring_view text) {
    if (text.empty()) return;
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return;
    const std::size_t at = out.size();
    out.resize(at + static_cast<std::size_t>(size));
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data() + at,
                        size, nullptr, nullptr);
}

std::wstring wide(std::string_view text) {
    std::wstring out;
    if (text.empty()) return out;
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return out;
    out.resize(static_cast<std::size_t>(size));
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

void append_line(std::string& out, char tag, std::wstring_view path) {
    // A path cannot hold a line break; one that somehow does is dropped, not split.
    if (path.empty() || path.find_first_of(L"\r\n") != std::wstring_view::npos) return;
    out.push_back(tag);
    out.push_back(' ');
    append_utf8(out, path);
    out.push_back('\n');
}

} // namespace

std::string PanelState::encode() const {
    std::string out(header);
    out.push_back('\n');
    append_line(out, 'S', selected);
    append_line(out, 'T', top);
    for (const std::wstring& path : expanded) append_line(out, 'E', path);
    return out;
}

PanelState PanelState::decode(std::string_view bytes) {
    PanelState state;
    std::size_t at = 0;
    bool first = true;
    while (at < bytes.size()) {
        std::size_t end = bytes.find('\n', at);
        if (end == std::string_view::npos) end = bytes.size();
        std::string_view line = bytes.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (first) {
            if (line != header) return {};
            first = false;
            continue;
        }
        if (line.size() < 3 || line[1] != ' ') continue;
        std::wstring path = wide(line.substr(2));
        if (path.empty()) continue;
        switch (line[0]) {
        case 'E': state.expanded.push_back(std::move(path)); break;
        case 'S': state.selected = std::move(path); break;
        case 'T': state.top = std::move(path); break;
        default: break;
        }
    }
    return state;
}

} // namespace filetree::settings
