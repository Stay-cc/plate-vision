#pragma once

#include "platevision/interfaces.hpp"

#include <opencv2/videoio.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace platevision {

class OpenCvMediaSource final : public IVideoSource {
public:
    explicit OpenCvMediaSource(std::filesystem::path path);

    bool open(std::string& error) override;
    bool read(Frame& frame, std::chrono::milliseconds timeout) override;
    void close() override;

private:
    std::filesystem::path path_;
    cv::VideoCapture capture_;
    cv::Mat image_;
    bool image_mode_{false};
    bool image_consumed_{false};
};

std::unique_ptr<IVideoSource> createOpenCvMediaSource(
    const std::filesystem::path& path,
    std::string& error);

}  // namespace platevision

