#pragma once
//
// Background work awaited with a deadline (the safety net of audio-device teardown).
//
// The work starts at once on its own detached thread. The caller waits for it at most until the
// deadline (waitUntil). If the work has not finished by then, the caller moves on, and the thread keeps
// running by itself and at the end frees what the work owns. So a stuck system call (e.g. the miniaudio
// thread of a vanished device) cannot freeze the caller thread (the GUI); the worst case is one leaked, stuck thread.
//
// The work must not refer to objects tied to the caller's lifetime: it must take everything by value or
// as its own property, because after the deadline the caller no longer waits for it.
#include <chrono>
#include <functional>
#include <memory>

namespace tanara::recording {

class DetachedJob {
public:
    using Clock = std::chrono::steady_clock;

    // Starts the work on a new, detached thread.
    explicit DetachedJob(std::function<void()> work);
    ~DetachedJob();

    DetachedJob(DetachedJob&&) noexcept;
    DetachedJob& operator=(DetachedJob&&) noexcept;
    DetachedJob(const DetachedJob&)            = delete;
    DetachedJob& operator=(const DetachedJob&) = delete;

    // Waits for the work to finish, at most until the deadline. True: the work finished.
    bool waitUntil(Clock::time_point deadline) const;
    bool waitFor(std::chrono::milliseconds timeout) const { return waitUntil(Clock::now() + timeout); }
    bool finished() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace tanara::recording