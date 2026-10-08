#pragma once

// Asynchronous folder listings: request on the main thread, enumerate on a worker, get the
// result back on the main thread. Cancellable per request.
//
// The main-thread hop is injected (`Poster`) so the offline tests can pump it themselves; the DLL
// passes fb2k::inMainThread (see fb2k_glue.cpp).

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "../platform/worker_pool.h"
#include "enumerate.h"

namespace filetree::fs {

class EnumerationService {
public:
    using Poster = std::function<void(std::function<void()>)>;
    using Callback = std::function<void(Listing&)>;

    //! Handle to one request. Cancellation is explicit (dropping a ticket does not cancel), so a
    //! panel cancels what it no longer wants: on collapse, on close.
    class Ticket {
    public:
        Ticket() = default;
        void cancel() const noexcept {
            if (flag_) flag_->store(true, std::memory_order_relaxed);
        }
        [[nodiscard]] bool valid() const noexcept { return flag_ != nullptr; }

    private:
        friend class EnumerationService;
        explicit Ticket(std::shared_ptr<std::atomic<bool>> flag) : flag_(std::move(flag)) {}
        std::shared_ptr<std::atomic<bool>> flag_;
    };

    EnumerationService(Poster poster, unsigned threads);

    //! Enumerates `folder` on a worker and calls `on_done` on the main thread with the listing,
    //! unless the ticket was cancelled first. Errors arrive as a listing with `error` set.
    Ticket request(std::wstring folder, EnumOptions options, Callback on_done);

    void shutdown(unsigned wait_ms) noexcept { pool_.shutdown(wait_ms); }

    [[nodiscard]] std::size_t pending() const noexcept { return pool_.pending(); }

private:
    Poster poster_;
    platform::WorkerPool pool_;
};

} // namespace filetree::fs
