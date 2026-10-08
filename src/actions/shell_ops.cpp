#include "shell_ops.h"

#include <atlbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <memory>
#include <utility>

#include "../fs/fb2k_glue.h"
#include "recycle_bin.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace filetree::actions {
namespace {

//! COM for one task on the shell worker. The worker thread is ours, so STA is safe to enter
//! and leave around each task.
class ComScope {
public:
    ComScope() noexcept
        : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComScope() {
        if (SUCCEEDED(hr_)) CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT hr_;
};

//! Shell dialogs need a top-level owner; the panel window is a child.
HWND top_level(HWND wnd) noexcept {
    HWND root = wnd != nullptr ? GetAncestor(wnd, GA_ROOT) : nullptr;
    return root != nullptr ? root : wnd;
}

void finish(ShellDone& done, ShellResult result) {
    if (!done) return;
    // std::function needs a copyable callable.
    auto shared = std::make_shared<ShellDone>(std::move(done));
    fs::post_to_main([shared, result] { (*shared)(result); });
}

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
