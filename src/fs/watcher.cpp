#include "watcher.h"

#include <chrono>
#include <cstddef>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace filetree::fs {
namespace {

constexpr DWORD notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                FILE_NOTIFY_CHANGE_ATTRIBUTES;
constexpr ULONG_PTR key_quit = 1;

struct Watch {
    WatchId id{};
    std::wstring path;
    HWND target{};
    UINT message{};
    HANDLE dir{INVALID_HANDLE_VALUE};
    OVERLAPPED overlapped{};
    bool closing{false}; //!< guarded by Service::lock
    // The changes themselves are not read, so a small buffer does: an overflow (0 bytes) is
    // reported like any other change.
    DWORD buffer[256]; // DWORD-aligned, as ReadDirectoryChangesW requires
};

class Service {
public:
    Service() = default;
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    // shutdown_watcher() runs at on_quit; a thread still here at unload is left to the OS.
    ~Service() {
        if (thread_.joinable()) thread_.detach();
    }

    WatchId watch(const std::wstring& path, HWND target, UINT message) {
        std::lock_guard guard(lock_);
        if (stopped_) return 0;
        if (port_ == nullptr) {
            port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
            if (port_ == nullptr) return 0;
            thread_ = std::thread([this] { run(); });
        }
        auto* w = new Watch;
        w->id = ++next_id_;
        w->path = path;
        w->target = target;
        w->message = message;
        watches_.emplace(w->id, w);
        // The open happens on the thread: key = the watch, no OVERLAPPED.
        if (!PostQueuedCompletionStatus(port_, 0, reinterpret_cast<ULONG_PTR>(w), nullptr)) {
            watches_.erase(w->id);
            delete w;
            return 0;
        }
        return w->id;
    }

    void unwatch(WatchId id) noexcept {
        std::lock_guard guard(lock_);
        const auto found = watches_.find(id);
        if (found == watches_.end()) return;
        Watch* w = found->second;
        w->closing = true;
        // A pending read completes as aborted; the thread frees the watch then. Without one
        // (still opening, or between reads) the thread sees `closing` on its next look.
        if (w->dir != INVALID_HANDLE_VALUE) CancelIoEx(w->dir, &w->overlapped);
    }

    std::size_t count() noexcept {
        std::lock_guard guard(lock_);
        return watches_.size();
    }

    void shutdown() noexcept {
        {
            std::lock_guard guard(lock_);
            if (stopped_) return;
            stopped_ = true;
            for (auto& [id, w] : watches_) {
                w->closing = true;
                if (w->dir != INVALID_HANDLE_VALUE) CancelIoEx(w->dir, &w->overlapped);
            }
            if (port_ != nullptr) PostQueuedCompletionStatus(port_, 0, key_quit, nullptr);
        }
        if (thread_.joinable()) thread_.join();
        if (port_ != nullptr) CloseHandle(port_);
        port_ = nullptr;
    }

private:
    //! Under lock_: forget and free a watch that has no read pending.
    void erase(Watch* w) noexcept {
        watches_.erase(w->id);
        if (w->dir != INVALID_HANDLE_VALUE) CloseHandle(w->dir);
        delete w;
    }

    //! Under lock_. False when the read could not start (the watch must end).
    bool listen(Watch* w) noexcept {
        w->overlapped = {};
        return ReadDirectoryChangesW(w->dir, w->buffer, sizeof(w->buffer), FALSE, notify_filter,
                                     nullptr, &w->overlapped, nullptr) != FALSE;
    }

    void open(Watch* w) noexcept {
        std::wstring path;
        {
            std::lock_guard guard(lock_);
            if (w->closing) {
                erase(w);
                return;
            }
            path = w->path;
        }
        // Outside the lock: this is the call that can take long (an offline network share).
        HANDLE dir = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                 nullptr);
        std::lock_guard guard(lock_);
        w->dir = dir;
        if (dir == INVALID_HANDLE_VALUE || w->closing ||
            CreateIoCompletionPort(dir, port_, reinterpret_cast<ULONG_PTR>(w), 0) == nullptr ||
            !listen(w)) {
            erase(w);
        }
    }

    void run() noexcept {
        auto quit_deadline = std::chrono::steady_clock::time_point::max();
        bool quitting = false;
        for (;;) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            const DWORD wait = quitting ? 50 : INFINITE;
            const BOOL ok = GetQueuedCompletionStatus(port_, &bytes, &key, &overlapped, wait);
            const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            if (quitting) {
                std::lock_guard guard(lock_);
                if (watches_.empty() || std::chrono::steady_clock::now() > quit_deadline) return;
            }
            if (overlapped == nullptr) {
                if (!ok) continue; // timeout while quitting
                if (key == key_quit) {
                    quitting = true;
                    quit_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
                    std::lock_guard guard(lock_);
                    if (watches_.empty()) return;
                    continue;
                }
                open(reinterpret_cast<Watch*>(key));
                continue;
            }
            auto* w = reinterpret_cast<Watch*>(key);
            std::lock_guard guard(lock_);
            if (w->closing || error == ERROR_OPERATION_ABORTED) {
                erase(w);
                continue;
            }
            // Something changed (or the buffer overflowed). An error such as access denied
            // usually means the folder itself went away: report it once and stop watching.
            PostMessageW(w->target, w->message, 0, static_cast<LPARAM>(w->id));
            if (error != ERROR_SUCCESS && error != ERROR_NOTIFY_ENUM_DIR) {
                erase(w);
            } else if (!listen(w)) {
                erase(w);
            }
        }
    }

    std::mutex lock_;
    HANDLE port_{};
    std::thread thread_;
    std::unordered_map<WatchId, Watch*> watches_;
    WatchId next_id_{0};
    bool stopped_{false};
};

Service& service() {
    static Service instance;
    return instance;
}

} // namespace

WatchId watch_folder(const std::wstring& path, HWND target, UINT message) {
    try {
        return service().watch(path, target, message);
    } catch (...) {
        return 0;
    }
}

void unwatch_folder(WatchId id) noexcept { service().unwatch(id); }

std::size_t watch_count() noexcept { return service().count(); }

void shutdown_watcher() noexcept { service().shutdown(); }

} // namespace filetree::fs
