#include "shell_items.h"

#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace filetree::actions {

HRESULT make_item_array(const std::vector<std::wstring>& paths, IShellItemArray** out) noexcept {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    if (paths.empty()) return E_INVALIDARG;
    HRESULT hr = S_OK;
    try {
        std::vector<PIDLIST_ABSOLUTE> pidls;
        pidls.reserve(paths.size());
        for (const std::wstring& path : paths) {
            PIDLIST_ABSOLUTE pidl = nullptr;
            hr = SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr);
            if (FAILED(hr) || pidl == nullptr) {
                if (SUCCEEDED(hr)) hr = E_FAIL;
                break;
            }
            pidls.push_back(pidl);
        }
        if (SUCCEEDED(hr)) {
            std::vector<PCIDLIST_ABSOLUTE> list(pidls.begin(), pidls.end());
            hr = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(list.size()), list.data(), out);
        }
        for (PIDLIST_ABSOLUTE pidl : pidls) CoTaskMemFree(pidl);
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    return hr;
}

HRESULT make_data_object(const std::vector<std::wstring>& paths, IDataObject** out) noexcept {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    IShellItemArray* items = nullptr;
    HRESULT hr = make_item_array(paths, &items);
    if (SUCCEEDED(hr)) {
        hr = items->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(out));
        items->Release();
    }
    return hr;
}

} // namespace filetree::actions
