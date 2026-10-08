#include "drag_out.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace filetree::actions {

bool drag_out(HWND source, const std::wstring& path) noexcept {
    if (path.empty()) return false;
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) || pidl == nullptr) {
        return false;
    }
    PCIDLIST_ABSOLUTE list[] = {pidl};
    Microsoft::WRL::ComPtr<IShellItemArray> items;
    Microsoft::WRL::ComPtr<IDataObject> data;
    HRESULT hr = SHCreateShellItemArrayFromIDLists(1, list, &items);
    if (SUCCEEDED(hr)) hr = items->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
    CoTaskMemFree(pidl);
    if (FAILED(hr) || !data) return false;

    // A null drop source gets the shell's default one (Esc cancels, cursors, drag image).
    DWORD effect = DROPEFFECT_NONE;
    SHDoDragDrop(source, data.Get(), nullptr, DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);
    return true;
}

} // namespace filetree::actions
