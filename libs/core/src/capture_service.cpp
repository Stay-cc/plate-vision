#include "platevision/capture_service.hpp"

#include <chrono>
#include <utility>

namespace platevision {
namespace {

std::int64_t nowMicroseconds() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

CaptureService::CaptureService(
    IVideoSource& source,
    BoundedLatestQueue<FrameRef>& queue,
    std::uint64_t stream_id,
    std::shared_ptr<PlaybackController> playback)
    : source_(source),
      queue_(queue),
      stream_id_(stream_id),
      playback_(std::move(playback)) {}

CaptureService::~CaptureService() {
    stop();
}

bool CaptureService::start(std::string& error) {
    if (running_.load()) {
        error = "capture service is already running";
        return false;
    }

    running_.store(true);
    thread_ = std::thread(&CaptureService::captureLoop, this);
    return true;
}

void CaptureService::stop() {
    running_.store(false);
    if (playback_) {
        playback_->setPaused(false);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    queue_.close();
}

bool CaptureService::running() const noexcept {
    return running_.load();
}

void CaptureService::captureLoop() {
    while (running_.load()) {
        if (playback_) {
            playback_->waitIfPaused();
        }
        const auto frame_started = std::chrono::steady_clock::now();
        Frame frame;
        if (!source_.read(frame, std::chrono::milliseconds(200))) {
            break;
        }

        const std::int64_t receive_time = nowMicroseconds();
        frame.stream_id = stream_id_;
        frame.sequence = next_sequence_++;
        frame.receive_timestamp_us = receive_time;
        if (frame.source_timestamp_us < 0) {
            frame.source_timestamp_us = 0;
        }

        auto frame_ref = std::make_shared<Frame>(std::move(frame));
        if (!queue_.pushLatest(std::move(frame_ref))) {
            break;
        }
        if (playback_) {
            playback_->sleepAfterFrame(frame_started);
        }
    }

    running_.store(false);
    queue_.close();
}

}  // namespace platevision

