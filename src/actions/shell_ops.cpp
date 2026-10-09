#include "shell_ops.h"

#include <atlbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <iterator>
#include <memory>
#include <utility>

#include "../fs/fb2k_glue.h"
#include "recycle_bin.h"
#include "shell_common.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace filetree::actions {
namespace {

using detail::ComScope;
using detail::finish;
using detail::top_level;

//! Runs one IFileOperation. Failures are reported by the shell's own UI.
template <typename Queue>
ShellResult run_file_operation(const std::wstring& path, HWND owner, DWORD flags, Queue&& queue) {
    CComPtr<IFileOperation> op;
    if (FAILED(op.CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL))) return {};
    CComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        return {};
    }
    if (owner != nullptr) op->SetOwnerWindow(owner);
    if (FAILED(op->SetOperationFlags(flags))) return {};
    if (FAILED(queue(*op, item.p))) return {};
    const HRESULT hr = op->PerformOperations();
    BOOL aborted = FALSE;
    op->GetAnyOperationsAborted(&aborted);
    return {true, SUCCEEDED(hr) && !aborted};
}

} // namespace

void open_in_explorer(std::wstring path, bool is_folder) noexcept {
    try {
        fs::shell_worker().submit([path = std::move(path), is_folder] {
            ComScope com;
            if (is_folder) {
                SHELLEXECUTEINFOW info{sizeof(info)};
                info.fMask = SEE_MASK_NOASYNC;
                info.lpFile = path.c_str();
                info.nShow = SW_SHOWNORMAL;
                ShellExecuteExW(&info);
                return;
            }
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) {
                SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0); // cidl 0: select pidl itself
                CoTaskMemFree(pidl);
            }
        });
    } catch (...) {
    }
}

bool copy_text(HWND owner, std::wstring_view text) noexcept {
    if (!OpenClipboard(owner)) return false;
    bool ok = false;
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (EmptyClipboard()) {
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes); memory != nullptr) {
            if (auto* target = static_cast<wchar_t*>(GlobalLock(memory)); target != nullptr) {
                std::copy(text.begin(), text.end(), target);
                target[text.size()] = L'\0';
                GlobalUnlock(memory);
                ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
            }
            if (!ok) GlobalFree(memory);
        }
    }
    CloseClipboard();
    return ok;
}

void delete_path(std::wstring path, bool permanent, HWND owner, ShellDone done) noexcept {
    try {
        fs::shell_worker().submit([path = std::move(path), permanent, owner = top_level(owner),
                                   done = std::move(done)]() mutable {
            ComScope com;
            const DWORD flags = permanent ? FOF_WANTNUKEWARNING
                                          : FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE;
            ShellResult result;
            try {
                result = run_file_operation(path, owner, flags, [](IFileOperation& op, IShellItem* item) {
                    return op.DeleteItem(item, nullptr);
                });
            } catch (...) {
            }
            finish(done, result);
        });
    } catch (...) {
    }
}

void rename_path(std::wstring path, std::wstring new_name, HWND owner, ShellDone done) noexcept {
    try {
        fs::shell_worker().submit([path = std::move(path), new_name = std::move(new_name),
                                   owner = top_level(owner), done = std::move(done)]() mutable {
            ComScope com;
            ShellResult result;
            try {
                result = run_file_operation(
                    path, owner, FOF_ALLOWUNDO, [&](IFileOperation& op, IShellItem* item) {
                        return op.RenameItem(item, new_name.c_str(), nullptr);
                    });
            } catch (...) {
            }
            finish(done, result);
        });
    } catch (...) {
    }
}

void show_properties(const std::wstring& path, HWND owner) noexcept {
    if (!path.empty()) SHObjectProperties(top_level(owner), SHOP_FILEPATH, path.c_str(), nullptr);
}

void open_with(const std::wstring& path, HWND owner) noexcept {
    if (path.empty()) return;
    OPENASINFO info{};
    info.pcszFile = path.c_str();
    info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_EXEC;
    SHOpenWithDialog(top_level(owner), &info);
}

bool pick_playlist_file(HWND owner, const std::wstring& folder, const std::wstring& name,
                        std::wstring& out) noexcept {
    CComPtr<IFileSaveDialog> dialog;
    if (FAILED(dialog.CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER))) {
        return false;
    }
    const COMDLG_FILTERSPEC types[] = {
        {L"M3U8 playlist", L"*.m3u8"},
        {L"foobar2000 playlist", L"*.fpl"},
        {L"M3U playlist", L"*.m3u"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(types)), types);
    dialog->SetFileTypeIndex(1);
    dialog->SetDefaultExtension(L"m3u8");
    dialog->SetTitle(L"Save folder as playlist");
    dialog->SetFileName(name.c_str());
    if (CComPtr<IShellItem> start;
        SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
        dialog->SetFolder(start);
    }
    if (FAILED(dialog->Show(top_level(owner)))) return false;
    CComPtr<IShellItem> result;
    PWSTR chosen = nullptr;
    if (FAILED(dialog->GetResult(&result)) ||
        FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &chosen))) {
        return false;
    }
    try {
        out = chosen;
    } catch (...) {
        CoTaskMemFree(chosen);
        return false;
    }
    CoTaskMemFree(chosen);
    return true;
}

void restore_recycled(std::wstring path, ShellDone done) noexcept {
    try {
        fs::shell_worker().submit([path = std::move(path), done = std::move(done)]() mutable {
            ShellResult result;
            try {
                result.succeeded = restore_from_recycle_bin(path);
                result.ran = result.succeeded;
            } catch (...) {
            }
            finish(done, result);
        });
    } catch (...) {
    }
}

} // namespace filetree::actions
