#include "platevision/playback_controller.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace platevision {

PlaybackController::PlaybackController(double target_fps)
    : target_fps_(std::max(1.0, target_fps)) {}

void PlaybackController::setPaused(bool paused) {
    paused_.store(paused);
    if (!paused) {
        condition_.notify_all();
    }
}

bool PlaybackController::paused() const noexcept {
    return paused_.load();
}

void PlaybackController::setSpeed(double speed) {
    speed_.store(std::clamp(speed, 0.25, 4.0));
}

double PlaybackController::speed() const noexcept {
    return speed_.load();
}

void PlaybackController::waitIfPaused() {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] {
        return !paused_.load();
    });
}

void PlaybackController::sleepAfterFrame(
    std::chrono::steady_clock::time_point frame_started) {
    const double frames_per_second =
        target_fps_ * std::max(0.25, speed_.load());
    const auto frame_duration =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / frames_per_second));
    const auto target = frame_started + frame_duration;
    const auto now = std::chrono::steady_clock::now();
    if (target > now) {
        std::this_thread::sleep_until(target);
    }
}

}  // namespace platevision

