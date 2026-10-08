#pragma once

// Small fixes shared by the panel's EDIT controls (filter box, typed path, inline rename).

#include <windows.h>

#include <algorithm>
#include <string>

namespace filetree::view::edit {

//! Ctrl+Backspace deletes the word before the caret, as in Explorer's boxes. A plain EDIT control
//! inserts a box character (0x7F) instead. Call from the subclass for WM_KEYDOWN and WM_CHAR;
//! returns true when the message was handled.
inline bool ctrl_backspace(HWND edit, UINT msg, WPARAM wp) noexcept {
    if (msg == WM_CHAR && wp == 0x7f) return true; // the box character itself
    if (msg != WM_KEYDOWN || wp != VK_BACK || GetKeyState(VK_CONTROL) >= 0) return false;
    DWORD start = 0;
    DWORD end = 0;
    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    if (start == end) {
        const int length = GetWindowTextLengthW(edit);
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(edit, text.data(), length + 1);
        const auto separator = [](wchar_t c) {
            return c == L' ' || c == L'\\' || c == L'/' || c == L'.' || c == L'-' || c == L'_' ||
                   c == L',' || c == L';';
        };
        DWORD at = std::min<DWORD>(end, static_cast<DWORD>(length));
        while (at > 0 && separator(text[at - 1])) --at;
        while (at > 0 && !separator(text[at - 1])) --at;
        start = at;
    }
    SendMessageW(edit, EM_SETSEL, start, end);
    SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
    return true;
}

} // namespace filetree::view::edit
