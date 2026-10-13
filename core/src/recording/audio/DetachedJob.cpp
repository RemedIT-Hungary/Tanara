#include "tanara/recording/audio/DetachedJob.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace tanara::recording {

struct DetachedJob::State {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
};

DetachedJob::DetachedJob(std::function<void()> work) : state_(std::make_shared<State>()) {
    // The thread keeps its own copy of the state: it lives on after the DetachedJob is gone.
    std::thread([state = state_, work = std::move(work)]() mutable {
        if (work) work();
        work = nullptr;   // resources the work owned are freed here, on the thread
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->done = true;
        }
        state->cv.notify_all();
    }).detach();
}

DetachedJob::~DetachedJob() = default;
DetachedJob::DetachedJob(DetachedJob&&) noexcept = default;
DetachedJob& DetachedJob::operator=(DetachedJob&&) noexcept = default;

bool DetachedJob::waitUntil(Clock::time_point deadline) const {
    if (!state_) return true;
    std::unique_lock<std::mutex> lock(state_->mutex);
    return state_->cv.wait_until(lock, deadline, [this] { return state_->done; });
}

bool DetachedJob::finished() const {
    if (!state_) return true;
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->done;
}

} // namespace tanara::recording