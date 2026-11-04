#pragma once

#include "platevision/interfaces.hpp"

#include <string>
#include <vector>

namespace platevision {

class MockPlateDetector final : public IPlateDetector {
public:
    explicit MockPlateDetector(float confidence = 0.99f);

    std::vector<PlateBox> detect(
        const Frame& frame,
        std::string& error) override;

private:
    float confidence_;
};

class PerspectiveRectifier final : public IPlateRectifier {
public:
    explicit PerspectiveRectifier(cv::Size output_size = {220, 70});

    cv::Mat rectify(
        const Frame& frame,
        const PlateBox& box) override;

private:
    cv::Size output_size_;
};

class MockPlateRecognizer final : public IPlateRecognizer {
public:
    MockPlateRecognizer(
        std::string text,
        PlateColor color,
        float confidence = 0.99f);

    PlateText recognize(
        const cv::Mat& plate_image,
        std::string& error) override;

private:
    std::string text_;
    PlateColor color_;
    float confidence_;
};

}  // namespace platevision

