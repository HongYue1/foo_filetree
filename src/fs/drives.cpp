#include "drives.h"

#include <windows.h>

namespace filetree::fs {

std::vector<std::wstring> drive_roots() {
    std::vector<std::wstring> roots;
    const DWORD mask = GetLogicalDrives();
    for (int letter = 0; letter < 26; ++letter) {
        if ((mask & (1u << letter)) == 0) continue;
        roots.push_back({static_cast<wchar_t>(L'A' + letter), L':', L'\\'});
    }
    return roots;
}

} // namespace filetree::fs
