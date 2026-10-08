#pragma once

// A small FIFO thread pool for blocking work (directory enumeration). Pure Win32 + std, so the
// offline tests use the same code as the DLL.
//
// Threads sleep on a condition variable when the queue is empty: zero CPU when idle. Shutdown
// waits a bounded time and then detaches whatever is still stuck (a dead network share can hold
// FindFirstFileExW for tens of seconds), so the pool never blocks foobar2000's exit. Detached
// threads keep the shared state alive by themselves.

#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace filetree::platform {

class WorkerPool {
public:
    using Task = std::move_only_function<void()>;

    explicit WorkerPool(unsigned thread_count);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    //! Queues a task. Ignored after shutdown(). Tasks must not throw; a throwing task is caught
    //! and dropped so one bad folder cannot take a worker down.
    void submit(Task task);

    //! Stops accepting work, drops queued tasks, and waits up to `wait_ms` for running ones.
    //! Idempotent.
    void shutdown(unsigned wait_ms) noexcept;

    //! Queued plus running tasks. For tests and the performance counters.
    [[nodiscard]] std::size_t pending() const noexcept;

private:
    struct State;
    static void run(std::shared_ptr<State> state);

    std::shared_ptr<State> state_;
    std::vector<std::thread> threads_;
};

} // namespace filetree::platform
