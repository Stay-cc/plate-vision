#include "platevision/video/opencv_sources.hpp"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>

namespace platevision {
namespace {

std::string lowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    return extension;
}

bool isImageExtension(const std::string& extension) {
    constexpr std::array<const char*, 8> kImageExtensions = {
        ".bmp", ".jpg", ".jpeg", ".png", ".tif", ".tiff", ".webp", ".ppm"};
    return std::find(
               kImageExtensions.begin(),
               kImageExtensions.end(),
               extension) != kImageExtensions.end();
}

}  // namespace

OpenCvMediaSource::OpenCvMediaSource(std::filesystem::path path)
    : path_(std::move(path)) {}

bool OpenCvMediaSource::open(std::string& error) {
    if (!std::filesystem::exists(path_)) {
        error = "input path does not exist: " + path_.string();
        return false;
    }

    image_mode_ = isImageExtension(lowerExtension(path_));
    image_consumed_ = false;

    if (image_mode_) {
        image_ = cv::imread(path_.string(), cv::IMREAD_COLOR);
        if (image_.empty()) {
            error = "failed to decode image: " + path_.string();
            return false;
        }
        return true;
    }

    if (!capture_.open(path_.string())) {
        error = "failed to open video source: " + path_.string();
        return false;
    }

    return true;
}

bool OpenCvMediaSource::read(
    Frame& frame,
    std::chrono::milliseconds /*timeout*/) {
    if (image_mode_) {
        if (image_consumed_ || image_.empty()) {
            return false;
        }
        image_consumed_ = true;
        frame.bgr = image_.clone();
        frame.source_timestamp_us = 0;
        return true;
    }

    cv::Mat image;
    if (!capture_.read(image) || image.empty()) {
        return false;
    }

    const double position_ms = capture_.get(cv::CAP_PROP_POS_MSEC);
    frame.bgr = std::move(image);
    frame.source_timestamp_us =
        position_ms > 0.0
            ? static_cast<std::int64_t>(position_ms * 1000.0)
            : 0;
    return true;
}

void OpenCvMediaSource::close() {
    capture_.release();
    image_.release();
    image_consumed_ = false;
}

std::unique_ptr<IVideoSource> createOpenCvMediaSource(
    const std::filesystem::path& path,
    std::string& error) {
    auto source = std::make_unique<OpenCvMediaSource>(path);
    if (!source->open(error)) {
        return nullptr;
    }
    return source;
}

}  // namespace platevision

