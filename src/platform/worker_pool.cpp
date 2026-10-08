#include "worker_pool.h"

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace filetree::platform {

struct WorkerPool::State {
    mutable std::mutex mutex;
    std::condition_variable wake;    //!< work arrived or stopping
    std::condition_variable drained; //!< a thread exited
    std::deque<Task> queue;
    std::size_t running{};
    unsigned alive{};
    bool stopping{};
};

WorkerPool::WorkerPool(unsigned thread_count) : state_(std::make_shared<State>()) {
    if (thread_count == 0) thread_count = 1;
    threads_.reserve(thread_count);
    for (unsigned i = 0; i < thread_count; ++i) {
        {
            std::lock_guard lock(state_->mutex);
            ++state_->alive;
        }
        threads_.emplace_back([state = state_] { run(state); });
    }
}

WorkerPool::~WorkerPool() { shutdown(2000); }

void WorkerPool::submit(Task task) {
    {
        std::lock_guard lock(state_->mutex);
        if (state_->stopping) return;
        state_->queue.push_back(std::move(task));
    }
    state_->wake.notify_one();
}

void WorkerPool::shutdown(unsigned wait_ms) noexcept {
    std::deque<Task> dropped;
    bool all_exited = false;
    {
        std::unique_lock lock(state_->mutex);
        if (state_->stopping && threads_.empty()) return;
        state_->stopping = true;
        dropped.swap(state_->queue);
        state_->wake.notify_all();
        all_exited = state_->drained.wait_for(lock, std::chrono::milliseconds(wait_ms),
                                              [this] { return state_->alive == 0; });
    }
    // Destroy dropped tasks outside the lock: their captures may do anything.
    dropped.clear();

    for (std::thread& thread : threads_) {
        if (all_exited) {
            thread.join();
        } else {
            thread.detach();
        }
    }
    threads_.clear();
}

std::size_t WorkerPool::pending() const noexcept {
    std::lock_guard lock(state_->mutex);
    return state_->queue.size() + state_->running;
}

void WorkerPool::run(std::shared_ptr<State> state) {
    // Enumeration is I/O-bound; stay out of the playback thread's way.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    std::unique_lock lock(state->mutex);
    for (;;) {
        state->wake.wait(lock, [&] { return state->stopping || !state->queue.empty(); });
        if (state->stopping) break;

        WorkerPool::Task task = std::move(state->queue.front());
        state->queue.pop_front();
        ++state->running;
        lock.unlock();
        try {
            task();
        } catch (...) {
        }
        task = nullptr; // release captures before taking the lock again
        lock.lock();
        --state->running;
    }
    --state->alive;
    state->drained.notify_all();
}

} // namespace filetree::platform
