#include "file_ops.h"

#include <atlbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <memory>
#include <utility>

#include "shell_common.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace filetree::actions {
namespace {

using detail::ComScope;
using detail::finish;
using detail::top_level;

std::wstring join(const std::wstring& folder, const std::wstring& name) {
    std::wstring out = folder;
    if (!out.empty() && out.back() != L'\\') out.push_back(L'\\');
    out += name;
    return out;
}

std::wstring parent_of(std::wstring path) {
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();
    const std::size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return {};
    if (slash == 2 && path[1] == L':') return path.substr(0, 3); // "C:\"
    return path.substr(0, slash);
}

bool same_path(const std::wstring& a, const std::wstring& b) noexcept {
    std::size_t la = a.size();
    std::size_t lb = b.size();
    while (la > 3 && a[la - 1] == L'\\') --la;
    while (lb > 3 && b[lb - 1] == L'\\') --lb;
    return CompareStringOrdinal(a.c_str(), static_cast<int>(la), b.c_str(), static_cast<int>(lb),
                                TRUE) == CSTR_EQUAL;
}

void set_drop_effect(IDataObject* data, const wchar_t* format_name, DWORD effect) noexcept {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (memory == nullptr) return;
    if (auto* p = static_cast<DWORD*>(GlobalLock(memory)); p != nullptr) {
        *p = effect;
        GlobalUnlock(memory);
    }
    FORMATETC fe{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(format_name)), nullptr,
                 DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = memory;
    if (FAILED(data->SetData(&fe, &medium, TRUE))) GlobalFree(memory); // TRUE: it owns it now
}

void append_hdrop(HDROP drop, std::vector<std::wstring>& out) {
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        if (length == 0) continue;
        std::wstring path(length, L'\0');
        DragQueryFileW(drop, i, path.data(), length + 1);
        out.push_back(std::move(path));
    }
}

} // namespace

void new_folder(std::wstring parent, HWND owner, NewFolderDone done) noexcept {
    try {
        fs::shell_worker().submit([parent = std::move(parent), owner = top_level(owner),
                                   done = std::move(done)]() mutable {
            ComScope com;
            ShellResult result;
            std::wstring name;
            try {
                // Explorer's naming: the first free of "New folder", "New folder (2)", ...
                name = L"New folder";
                for (int n = 2; n < 1000 && GetFileAttributesW(join(parent, name).c_str()) !=
                                                INVALID_FILE_ATTRIBUTES;
                     ++n) {
                    name = L"New folder (" + std::to_wstring(n) + L")";
                }
                CComPtr<IFileOperation> op;
                CComPtr<IShellItem> folder;
                if (SUCCEEDED(op.CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL)) &&
                    SUCCEEDED(SHCreateItemFromParsingName(parent.c_str(), nullptr,
                                                          IID_PPV_ARGS(&folder)))) {
                    if (owner != nullptr) op->SetOwnerWindow(owner);
                    if (SUCCEEDED(op->SetOperationFlags(FOF_ALLOWUNDO)) &&
                        SUCCEEDED(op->NewItem(folder, FILE_ATTRIBUTE_DIRECTORY, name.c_str(),
                                              nullptr, nullptr))) {
                        const HRESULT hr = op->PerformOperations();
                        BOOL aborted = FALSE;
                        op->GetAnyOperationsAborted(&aborted);
                        result = {true, SUCCEEDED(hr) && !aborted};
                    }
                }
            } catch (...) {
            }
            if (!result.succeeded) name.clear();
            finish(done, result, std::move(name));
        });
    } catch (...) {
    }
}

void copy_items(std::vector<std::wstring> paths, std::wstring folder, bool move, HWND owner,
                ShellDone done) noexcept {
    try {
        fs::shell_worker().submit([paths = std::move(paths), folder = std::move(folder), move,
                                   owner = top_level(owner), done = std::move(done)]() mutable {
            ComScope com;
            ShellResult result;
            try {
                bool all_here = true;
                for (const std::wstring& path : paths) {
                    all_here = all_here && same_path(parent_of(path), folder);
                }
                if (paths.empty() || (move && all_here)) {
                    finish(done, result);
                    return;
                }
                std::vector<PIDLIST_ABSOLUTE> pidls;
                pidls.reserve(paths.size());
                for (const std::wstring& path : paths) {
                    PIDLIST_ABSOLUTE pidl = nullptr;
                    if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) &&
                        pidl != nullptr) {
                        pidls.push_back(pidl);
                    }
                }
                CComPtr<IShellItemArray> items;
                CComPtr<IShellItem> target;
                CComPtr<IFileOperation> op;
                if (!pidls.empty() &&
                    SUCCEEDED(SHCreateShellItemArrayFromIDLists(
                        static_cast<UINT>(pidls.size()),
                        const_cast<PCIDLIST_ABSOLUTE_ARRAY>(pidls.data()), &items)) &&
                    SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr,
                                                          IID_PPV_ARGS(&target))) &&
                    SUCCEEDED(op.CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL))) {
                    if (owner != nullptr) op->SetOwnerWindow(owner);
                    DWORD flags = FOF_ALLOWUNDO;
                    if (!move && all_here) flags |= FOF_RENAMEONCOLLISION; // "name - Copy"
                    const HRESULT queued = FAILED(op->SetOperationFlags(flags)) ? E_FAIL
                                           : move ? op->MoveItems(items, target)
                                                  : op->CopyItems(items, target);
                    if (SUCCEEDED(queued)) {
                        const HRESULT hr = op->PerformOperations();
                        BOOL aborted = FALSE;
                        op->GetAnyOperationsAborted(&aborted);
                        result = {true, SUCCEEDED(hr) && !aborted};
                    }
                }
                for (PIDLIST_ABSOLUTE pidl : pidls) CoTaskMemFree(pidl);
            } catch (...) {
            }
            finish(done, result);
        });
    } catch (...) {
    }
}

bool set_clipboard_file(const std::wstring& path, bool cut) noexcept {
    if (path.empty()) return false;
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) || pidl == nullptr) {
        return false;
    }
    PCIDLIST_ABSOLUTE list[] = {pidl};
    CComPtr<IShellItemArray> items;
    CComPtr<IDataObject> data;
    HRESULT hr = SHCreateShellItemArrayFromIDLists(1, list, &items);
    if (SUCCEEDED(hr)) hr = items->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
    CoTaskMemFree(pidl);
    if (FAILED(hr) || !data) return false;
    set_drop_effect(data, CFSTR_PREFERREDDROPEFFECT, cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY);
    if (FAILED(OleSetClipboard(data))) return false;
    // Render it now: the clipboard keeps working after this panel or foobar2000 is gone.
    OleFlushClipboard();
    return true;
}

bool clipboard_has_files() noexcept { return IsClipboardFormatAvailable(CF_HDROP) != FALSE; }

bool read_clipboard_files(HWND owner, ClipboardFiles& out) noexcept {
    out = {};
    try {
        if (!OpenClipboard(owner)) return false;
        out.sequence = GetClipboardSequenceNumber();
        if (auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP)); drop != nullptr) {
            append_hdrop(drop, out.paths);
        }
        const auto format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        if (HANDLE effect = GetClipboardData(format); effect != nullptr) {
            if (const auto* p = static_cast<const DWORD*>(GlobalLock(effect)); p != nullptr) {
                out.cut = (*p & DROPEFFECT_MOVE) != 0;
                GlobalUnlock(effect);
            }
        }
        CloseClipboard();
    } catch (...) {
        CloseClipboard();
        return false;
    }
    return !out.paths.empty();
}

void clear_clipboard_if(HWND owner, DWORD sequence) noexcept {
    if (GetClipboardSequenceNumber() != sequence || !OpenClipboard(owner)) return;
    EmptyClipboard();
    CloseClipboard();
}

bool has_files(IDataObject* data) noexcept {
    if (data == nullptr) return false;
    FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return data->QueryGetData(&fe) == S_OK;
}

bool read_files(IDataObject* data, std::vector<std::wstring>& out) noexcept {
    out.clear();
    if (data == nullptr) return false;
    FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&fe, &medium))) return false;
    try {
        if (auto drop = static_cast<HDROP>(GlobalLock(medium.hGlobal)); drop != nullptr) {
            append_hdrop(drop, out);
            GlobalUnlock(medium.hGlobal);
        }
    } catch (...) {
    }
    ReleaseStgMedium(&medium);
    return !out.empty();
}

void report_optimized_move(IDataObject* data) noexcept {
    if (data == nullptr) return;
    set_drop_effect(data, CFSTR_PERFORMEDDROPEFFECT, DROPEFFECT_NONE);
    set_drop_effect(data, CFSTR_LOGICALPERFORMEDDROPEFFECT, DROPEFFECT_MOVE);
}

} // namespace filetree::actions
