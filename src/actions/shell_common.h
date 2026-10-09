#pragma once

// Shared by the shell operations (shell_ops.cpp, file_ops.cpp): COM per worker task, dialog
// owners, completion back on the main thread.

#include <windows.h>

#include <objbase.h>

#include <memory>
#include <utility>

#include "../fs/fb2k_glue.h"

namespace filetree::actions::detail {

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
inline HWND top_level(HWND wnd) noexcept {
    HWND root = wnd != nullptr ? GetAncestor(wnd, GA_ROOT) : nullptr;
    return root != nullptr ? root : wnd;
}

//! Calls `done(args...)` on the main thread (if set).
template <typename Done, typename... Args>
void finish(Done& done, Args... args) {
    if (!done) return;
    // std::function needs a copyable callable.
    auto shared = std::make_shared<Done>(std::move(done));
    fs::post_to_main([shared, args...] { (*shared)(args...); });
}

} // namespace filetree::actions::detail
