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

    // Copy unless Shift asks for a move: Explorer and other shell targets take their default
    // effect from the preferred one, so a plain drop never moves the user's files.
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD)); memory != nullptr) {
        if (auto* p = static_cast<DWORD*>(GlobalLock(memory)); p != nullptr) {
            *p = DROPEFFECT_COPY;
            GlobalUnlock(memory);
        }
        FORMATETC fe{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)),
                     nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        medium.tymed = TYMED_HGLOBAL;
        medium.hGlobal = memory;
        if (FAILED(data->SetData(&fe, &medium, TRUE))) GlobalFree(memory);
    }
    // A null drop source gets the shell's default one (Esc cancels, cursors, drag image).
    DWORD effect = DROPEFFECT_NONE;
    SHDoDragDrop(source, data.Get(), nullptr, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK,
                 &effect);
    return true;
}

} // namespace filetree::actions
