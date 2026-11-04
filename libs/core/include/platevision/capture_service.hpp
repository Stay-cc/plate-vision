#pragma once

#include "platevision/bounded_queue.hpp"
#include "platevision/interfaces.hpp"
#include "platevision/playback_controller.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace platevision {

class CaptureService final {
public:
    CaptureService(
        IVideoSource& source,
        BoundedLatestQueue<FrameRef>& queue,
        std::uint64_t stream_id = 0,
        std::shared_ptr<PlaybackController> playback = nullptr);

    ~CaptureService();

    CaptureService(const CaptureService&) = delete;
    CaptureService& operator=(const CaptureService&) = delete;

    bool start(std::string& error);
    void stop();
    bool running() const noexcept;

private:
    void captureLoop();

    IVideoSource& source_;
    BoundedLatestQueue<FrameRef>& queue_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::uint64_t stream_id_{0};
    std::uint64_t next_sequence_{0};
    std::shared_ptr<PlaybackController> playback_;
};

}  // namespace platevision

