#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace platevision {

class PlaybackController final {
public:
    explicit PlaybackController(double target_fps);

    void setPaused(bool paused);
    bool paused() const noexcept;

    void setSpeed(double speed);
    double speed() const noexcept;

    void waitIfPaused();
    void sleepAfterFrame(
        std::chrono::steady_clock::time_point frame_started);

private:
    double target_fps_;
    std::atomic<bool> paused_{false};
    std::atomic<double> speed_{1.0};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
};

}  // namespace platevision

