#include "drop_target.h"

#include <shlobj.h>

#include <new>

#pragma comment(lib, "ole32.lib")

namespace filetree::view {

DropTarget::DropTarget(HWND wnd, DropSink& sink) noexcept : wnd_(wnd), sink_(&sink) {
    CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&helper_));
}

DropTarget::~DropTarget() {
    release_data();
    if (helper_ != nullptr) helper_->Release();
}

DropTarget* DropTarget::attach(HWND wnd, DropSink& sink, HRESULT& hr) noexcept {
    hr = E_OUTOFMEMORY;
    auto* target = new (std::nothrow) DropTarget(wnd, sink);
    if (target == nullptr) return nullptr;
    hr = RegisterDragDrop(wnd, target);
    if (FAILED(hr)) {
        target->Release();
        return nullptr;
    }
    return target;
}

void DropTarget::detach() noexcept {
    sink_ = nullptr;
    RevokeDragDrop(wnd_); // drops OLE's reference
    release_data();
    Release();
}

HRESULT DropTarget::QueryInterface(REFIID riid, void** out) noexcept {
    if (out == nullptr) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *out = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG DropTarget::AddRef() noexcept { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }

ULONG DropTarget::Release() noexcept {
    const LONG refs = InterlockedDecrement(&refs_);
    if (refs == 0) delete this;
    return static_cast<ULONG>(refs);
}

POINT DropTarget::client(POINTL point) const noexcept {
    POINT p{point.x, point.y};
    ScreenToClient(wnd_, &p);
    return p;
}

void DropTarget::release_data() noexcept {
    if (data_ != nullptr) {
        data_->Release();
        data_ = nullptr;
    }
}

HRESULT DropTarget::DragEnter(IDataObject* data, DWORD keys, POINTL point,
                              DWORD* effect) noexcept {
    if (effect == nullptr) return E_INVALIDARG;
    release_data();
    data_ = data;
    if (data_ != nullptr) data_->AddRef();
    *effect = sink_ != nullptr ? sink_->drag_over(data, keys, client(point), *effect, true)
                               : DROPEFFECT_NONE;
    POINT p{point.x, point.y};
    if (helper_ != nullptr) helper_->DragEnter(wnd_, data, &p, *effect);
    return S_OK;
}

HRESULT DropTarget::DragOver(DWORD keys, POINTL point, DWORD* effect) noexcept {
    if (effect == nullptr) return E_INVALIDARG;
    *effect = sink_ != nullptr ? sink_->drag_over(data_, keys, client(point), *effect, false)
                               : DROPEFFECT_NONE;
    POINT p{point.x, point.y};
    if (helper_ != nullptr) helper_->DragOver(&p, *effect);
    return S_OK;
}

HRESULT DropTarget::DragLeave() noexcept {
    if (sink_ != nullptr) sink_->drag_leave();
    if (helper_ != nullptr) helper_->DragLeave();
    release_data();
    return S_OK;
}

HRESULT DropTarget::Drop(IDataObject* data, DWORD keys, POINTL point, DWORD* effect) noexcept {
    if (effect == nullptr) return E_INVALIDARG;
    // The sink only queues work (the shell's UI runs on a worker), so the image goes right after.
    *effect = sink_ != nullptr ? sink_->drop(data, keys, client(point), *effect) : DROPEFFECT_NONE;
    POINT p{point.x, point.y};
    if (helper_ != nullptr) helper_->Drop(data, &p, *effect);
    release_data();
    return S_OK;
}

} // namespace filetree::view
