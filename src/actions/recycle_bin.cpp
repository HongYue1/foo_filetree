#include "recycle_bin.h"

#include <windows.h>

#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace filetree::actions {
namespace {

std::wstring user_sid() {
    std::wstring out;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return out;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<std::uint8_t> buffer(size);
    if (size != 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,
                                   &text)) {
            out = text;
            LocalFree(text);
        }
    }
    CloseHandle(token);
    return out;
}

//! Reads a `$I` record. Returns false if it is not one we understand.
bool read_index(const std::wstring& file, std::vector<std::uint8_t>& data, std::wstring& original,
                std::int64_t& deleted) {
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD read = 0;
    const BOOL ok = ReadFile(handle, data.data(), static_cast<DWORD>(data.size()), &read, nullptr);
    CloseHandle(handle);
    if (!ok || read < 28) return false;

    std::int64_t version = 0;
    std::memcpy(&version, data.data(), 8);
    std::memcpy(&deleted, data.data() + 16, 8);
    std::size_t offset = 0;
    std::size_t max_chars = 0;
    if (version == 1) {
        offset = 24;
        max_chars = 260;
    } else if (version == 2) {
        std::uint32_t length = 0;
        std::memcpy(&length, data.data() + 24, 4);
        offset = 28;
        max_chars = length;
    } else {
        return false;
    }
    max_chars = std::min<std::size_t>(max_chars, (read - offset) / 2);
    original.clear();
    for (std::size_t i = 0; i < max_chars; ++i) {
        wchar_t c;
        std::memcpy(&c, data.data() + offset + 2 * i, 2);
        if (c == L'\0') break;
        original.push_back(c);
    }
    return true;
}

} // namespace

bool restore_from_recycle_bin(const std::wstring& path) {
    if (path.size() < 4 || path[1] != L':' || path[2] != L'\\') return false;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    const std::wstring sid = user_sid();
    if (sid.empty()) return false;

    const std::wstring folder = path.substr(0, 3) + L"$Recycle.Bin\\" + sid + L"\\";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW((folder + L"$I*").c_str(), FindExInfoBasic, &data,
                                   FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return false;

    std::vector<std::uint8_t> buffer(28 + 2 * 32768);
    std::wstring best;
    std::int64_t best_time = 0;
    std::wstring original;
    do {
        std::int64_t deleted = 0;
        if (!read_index(folder + data.cFileName, buffer, original, deleted)) continue;
        if (CompareStringOrdinal(original.c_str(), static_cast<int>(original.size()),
                                 path.c_str(), static_cast<int>(path.size()),
                                 TRUE) != CSTR_EQUAL) {
            continue;
        }
        if (best.empty() || deleted > best_time) {
            best = data.cFileName;
            best_time = deleted;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (best.size() < 3) return false;

    const std::wstring index = folder + best;
    const std::wstring item = folder + L"$R" + best.substr(2);
    if (!MoveFileExW(item.c_str(), path.c_str(), 0)) return false;
    DeleteFileW(index.c_str());
    // Let Explorer windows (and the Recycle Bin view) update.
    const DWORD attributes = GetFileAttributesW(path.c_str());
    const bool is_folder = attributes != INVALID_FILE_ATTRIBUTES &&
                           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    SHChangeNotify(is_folder ? SHCNE_MKDIR : SHCNE_CREATE, SHCNF_PATHW, path.c_str(), nullptr);
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, folder.c_str(), nullptr);
    return true;
}

} // namespace filetree::actions
