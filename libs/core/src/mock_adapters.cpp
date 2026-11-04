#include "platevision/mock_adapters.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace platevision {

MockPlateDetector::MockPlateDetector(float confidence)
    : confidence_(confidence) {}

std::vector<PlateBox> MockPlateDetector::detect(
    const Frame& frame,
    std::string& error) {
    if (frame.bgr.empty()) {
        error = "input frame is empty";
        return {};
    }

    const float width = static_cast<float>(frame.bgr.cols);
    const float height = static_cast<float>(frame.bgr.rows);
    const float plate_width = width * 0.24f;
    const float plate_height = height * 0.075f;
    const float left = (width - plate_width) * 0.5f;
    const float top = height * 0.56f;

    PlateBox box;
    box.bounds = cv::Rect2f(left, top, plate_width, plate_height);
    box.quad = {
        cv::Point2f(left, top),
        cv::Point2f(left + plate_width, top),
        cv::Point2f(left + plate_width, top + plate_height),
        cv::Point2f(left, top + plate_height)};
    box.confidence = confidence_;
    return {box};
}

PerspectiveRectifier::PerspectiveRectifier(cv::Size output_size)
    : output_size_(output_size) {}

cv::Mat PerspectiveRectifier::rectify(
    const Frame& frame,
    const PlateBox& box) {
    if (frame.bgr.empty() || output_size_.width <= 0 ||
        output_size_.height <= 0) {
        return {};
    }

    const cv::Rect2f clamped = box.bounds &
        cv::Rect2f(0.0f,
                   0.0f,
                   static_cast<float>(frame.bgr.cols),
                   static_cast<float>(frame.bgr.rows));
    if (clamped.width <= 1.0f || clamped.height <= 1.0f) {
        return {};
    }

    const float quad_area = static_cast<float>(
        std::abs(cv::contourArea(std::vector<cv::Point2f>(
            box.quad.begin(),
            box.quad.end()))));

    if (quad_area > 1.0f) {
        const std::vector<cv::Point2f> source = {
            box.quad[0], box.quad[1], box.quad[2], box.quad[3]};
        const std::vector<cv::Point2f> destination = {
            cv::Point2f(0.0f, 0.0f),
            cv::Point2f(static_cast<float>(output_size_.width), 0.0f),
            cv::Point2f(static_cast<float>(output_size_.width),
                        static_cast<float>(output_size_.height)),
            cv::Point2f(0.0f, static_cast<float>(output_size_.height))};
        const cv::Mat transform =
            cv::getPerspectiveTransform(source, destination);
        cv::Mat rectified;
        cv::warpPerspective(
            frame.bgr,
            rectified,
            transform,
            output_size_,
            cv::INTER_LINEAR);
        return rectified;
    }

    const cv::Mat crop = frame.bgr(
        cv::Rect(static_cast<int>(clamped.x),
                 static_cast<int>(clamped.y),
                 static_cast<int>(clamped.width),
                 static_cast<int>(clamped.height)));
    cv::Mat rectified;
    cv::resize(crop, rectified, output_size_);
    return rectified;
}

MockPlateRecognizer::MockPlateRecognizer(
    std::string text,
    PlateColor color,
    float confidence)
    : text_(std::move(text)), color_(color), confidence_(confidence) {}

PlateText MockPlateRecognizer::recognize(
    const cv::Mat& plate_image,
    std::string& error) {
    if (plate_image.empty()) {
        error = "rectified plate image is empty";
        return {};
    }

    PlateText result;
    result.text = text_;
    result.color = color_;
    result.confidence = confidence_;
    return result;
}

}  // namespace platevision

