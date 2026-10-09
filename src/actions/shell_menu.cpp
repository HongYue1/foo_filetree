#include "shell_menu.h"

#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace filetree::actions {
namespace {

template <typename T>
void release(T*& p) noexcept {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

void add_placeholder(HMENU menu) noexcept {
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"(unavailable)");
}

} // namespace

void ShellMenu::prepare(HMENU submenu, std::vector<std::wstring> paths, HWND owner,
                        bool extended) {
    reset();
    submenu_ = submenu;
    paths_ = std::move(paths);
    owner_ = owner;
    extended_ = extended;
}

void ShellMenu::reset() noexcept {
    release(menu3_);
    release(menu2_);
    release(menu_);
    submenu_ = nullptr;
    populated_ = false;
}

bool ShellMenu::populate() noexcept {
    populated_ = true;
    if (paths_.empty()) return false;
    // All items are children of the first one's parent (the caller checked).
    std::vector<PIDLIST_ABSOLUTE> pidls;
    std::vector<PCUITEMID_CHILD> children;
    HRESULT hr = S_OK;
    try {
        pidls.reserve(paths_.size());
        children.reserve(paths_.size());
        for (const std::wstring& path : paths_) {
            PIDLIST_ABSOLUTE pidl = nullptr;
            hr = SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr);
            if (FAILED(hr) || pidl == nullptr) break;
            pidls.push_back(pidl);
            children.push_back(ILFindLastID(pidl));
        }
    } catch (...) {
        hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr) && !pidls.empty()) {
        IShellFolder* parent = nullptr;
        hr = SHBindToParent(pidls[0], IID_PPV_ARGS(&parent), nullptr);
        if (SUCCEEDED(hr)) {
            hr = parent->GetUIObjectOf(owner_, static_cast<UINT>(children.size()), children.data(),
                                       IID_IContextMenu, nullptr, reinterpret_cast<void**>(&menu_));
            parent->Release();
        }
    }
    for (PIDLIST_ABSOLUTE pidl : pidls) CoTaskMemFree(pidl);
    if (FAILED(hr) || menu_ == nullptr) return false;

    menu_->QueryInterface(IID_PPV_ARGS(&menu3_));
    if (menu3_ == nullptr) menu_->QueryInterface(IID_PPV_ARGS(&menu2_));

    // No CMF_CANRENAME: the shell's Rename would need an Explorer view; ours is F2.
    UINT flags = CMF_NORMAL | CMF_EXPLORE;
    if (extended_) flags |= CMF_EXTENDEDVERBS;
    if (FAILED(menu_->QueryContextMenu(submenu_, 0, first_id, last_id, flags))) return false;
    if (!extended_) {
        // Explorer's Shift-only verbs (Copy as path, Open PowerShell here, ...) are easy to miss.
        AppendMenuW(submenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(submenu_, MF_STRING | MF_GRAYED, 0, L"Hold Shift while right-clicking for more");
    }
    return true;
}

bool ShellMenu::handle_message(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept {
    if (submenu_ == nullptr) return false;
    if (msg == WM_INITMENUPOPUP && reinterpret_cast<HMENU>(wp) == submenu_) {
        if (!populated_ && !populate()) add_placeholder(submenu_);
        result = 0;
        return true;
    }
    if (!populated_) return false;
    // Owner-draw items and the shell's own submenus.
    if (menu3_ != nullptr) {
        LRESULT out = 0;
        if (SUCCEEDED(menu3_->HandleMenuMsg2(msg, wp, lp, &out))) {
            result = out;
            return true;
        }
        return false;
    }
    if (menu2_ != nullptr && msg != WM_MENUCHAR) {
        if (SUCCEEDED(menu2_->HandleMenuMsg(msg, wp, lp))) {
            result = msg == WM_INITMENUPOPUP ? 0 : TRUE;
            return true;
        }
    }
    return false;
}

void ShellMenu::invoke(UINT id, POINT point) noexcept {
    if (menu_ == nullptr || !owns(id)) return;
    CMINVOKECOMMANDINFOEX info{};
    info.cbSize = sizeof(info);
    info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
    if (GetKeyState(VK_CONTROL) < 0) info.fMask |= CMIC_MASK_CONTROL_DOWN;
    if (GetKeyState(VK_SHIFT) < 0) info.fMask |= CMIC_MASK_SHIFT_DOWN;
    info.hwnd = owner_;
    info.lpVerb = MAKEINTRESOURCEA(id - first_id);
    info.lpVerbW = MAKEINTRESOURCEW(id - first_id);
    info.nShow = SW_SHOWNORMAL;
    info.ptInvoke = point;
    menu_->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
}

} // namespace filetree::actions
