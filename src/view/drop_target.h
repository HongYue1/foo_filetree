#pragma once

// OLE drop target for a window: forwards to a DropSink and shows the shell's drag image
// (IDropTargetHelper) over it. Main thread only.

#include <windows.h>

#include <oleidl.h>
#include <shobjidl.h>

namespace filetree::view {

class DropSink {
public:
    //! Enter or move. `point` is in client coordinates. Returns the effect to show.
    virtual DWORD drag_over(IDataObject* data, DWORD keys, POINT point, DWORD allowed,
                            bool enter) noexcept = 0;
    virtual void drag_leave() noexcept = 0;
    //! Returns the effect performed (DROPEFFECT_NONE after an optimized move).
    virtual DWORD drop(IDataObject* data, DWORD keys, POINT point, DWORD allowed) noexcept = 0;

protected:
    ~DropSink() = default;
};

class DropTarget final : public IDropTarget {
public:
    //! Registers a new target for `wnd` (RegisterDragDrop). Null if OLE refused.
    static DropTarget* attach(HWND wnd, DropSink& sink, HRESULT& hr) noexcept;
    //! Revokes it and lets go of the sink; releases the caller's reference.
    void detach() noexcept;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL point,
                                        DWORD* effect) noexcept override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL point, DWORD* effect) noexcept override;
    HRESULT STDMETHODCALLTYPE DragLeave() noexcept override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL point,
                                   DWORD* effect) noexcept override;

private:
    DropTarget(HWND wnd, DropSink& sink) noexcept;
    ~DropTarget();
    DropTarget(const DropTarget&) = delete;
    DropTarget& operator=(const DropTarget&) = delete;

    [[nodiscard]] POINT client(POINTL point) const noexcept;
    void release_data() noexcept;

    LONG refs_{1};
    HWND wnd_;
    DropSink* sink_;
    IDropTargetHelper* helper_{};
    IDataObject* data_{}; //!< between DragEnter and DragLeave / Drop
};

} // namespace filetree::view
