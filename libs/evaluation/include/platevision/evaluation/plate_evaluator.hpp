#pragma once

#include "platevision/interfaces.hpp"
#include "platevision/plate_validator.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace platevision {

struct EvaluationSample {
    std::filesystem::path image_path;
    std::string expected_plate;
    PlateColor expected_color{PlateColor::Unknown};
    std::optional<cv::Rect2f> expected_box;
};

struct EvaluationResult {
    EvaluationSample sample;
    bool image_loaded{false};
    bool detection_found{false};
    PlateBox detection;
    std::optional<float> detection_iou;
    std::string predicted_plate;
    PlateColor predicted_color{PlateColor::Unknown};
    float confidence{0.0f};
    bool valid{false};
    bool exact_match{false};
    double character_accuracy{0.0};
    std::int64_t latency_us{0};
    std::string error;
};

class PlateEvaluator final {
public:
    PlateEvaluator(
        IPlateDetector& detector,
        IPlateRectifier& rectifier,
        IPlateRecognizer& recognizer);

    EvaluationResult evaluate(const EvaluationSample& sample);

private:
    IPlateDetector& detector_;
    IPlateRectifier& rectifier_;
    IPlateRecognizer& recognizer_;
    PlateValidator validator_;
};

class CropPlateEvaluator final {
public:
    explicit CropPlateEvaluator(IPlateRecognizer& recognizer);

    EvaluationResult evaluate(const EvaluationSample& sample);

private:
    IPlateRecognizer& recognizer_;
    PlateValidator validator_;
};

}  // namespace platevision

