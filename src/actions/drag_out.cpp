#include "drag_out.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include "shell_items.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace filetree::actions {

bool drag_out(HWND source, const std::vector<std::wstring>& paths, bool allow_move) noexcept {
    Microsoft::WRL::ComPtr<IDataObject> data;
    if (FAILED(make_data_object(paths, &data)) || !data) return false;

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
    const DWORD allowed =
        DROPEFFECT_COPY | DROPEFFECT_LINK | (allow_move ? DROPEFFECT_MOVE : DROPEFFECT_NONE);
    SHDoDragDrop(source, data.Get(), nullptr, allowed, &effect);
    return true;
}

} // namespace filetree::actions
