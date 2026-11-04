#include "platevision/evaluation/plate_evaluator.hpp"

#include "platevision/evaluation/plate_metrics.hpp"
#include "platevision/plate_validator.hpp"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace platevision {
namespace {

using Clock = std::chrono::steady_clock;

}  // namespace

PlateEvaluator::PlateEvaluator(
    IPlateDetector& detector,
    IPlateRectifier& rectifier,
    IPlateRecognizer& recognizer)
    : detector_(detector),
      rectifier_(rectifier),
      recognizer_(recognizer) {}

CropPlateEvaluator::CropPlateEvaluator(
    IPlateRecognizer& recognizer)
    : recognizer_(recognizer) {}

EvaluationResult PlateEvaluator::evaluate(
    const EvaluationSample& sample) {
    const auto started = Clock::now();
    EvaluationResult result;
    result.sample = sample;

    std::ifstream input(sample.image_path, std::ios::binary);
    const std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const cv::Mat image = bytes.empty()
        ? cv::Mat{}
        : cv::imdecode(bytes, cv::IMREAD_COLOR);
    if (image.empty()) {
        result.error = "failed to load image: " +
            sample.image_path.string();
        return result;
    }
    result.image_loaded = true;

    Frame frame;
    frame.bgr = image;
    frame.source_timestamp_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            started.time_since_epoch())
            .count();

    std::string error;
    const std::vector<PlateBox> detections =
        detector_.detect(frame, error);
    if (!error.empty()) {
        result.error = std::move(error);
        return result;
    }
    if (detections.empty()) {
        result.error = "no plate detection";
        result.latency_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - started)
                .count();
        return result;
    }

    std::size_t selected_index = 0;
    if (sample.expected_box) {
        float best_iou = -1.0f;
        for (std::size_t index = 0; index < detections.size(); ++index) {
            const cv::Rect2f& box = detections[index].bounds;
            const float iou = PlateMetrics::intersectionOverUnion(
                box.x,
                box.y,
                box.width,
                box.height,
                sample.expected_box->x,
                sample.expected_box->y,
                sample.expected_box->width,
                sample.expected_box->height);
            if (iou > best_iou) {
                best_iou = iou;
                selected_index = index;
            }
        }
        result.detection_iou = best_iou;
    }

    result.detection_found = true;
    result.detection = detections[selected_index];

    const cv::Mat plate_image =
        rectifier_.rectify(frame, result.detection);
    if (plate_image.empty()) {
        result.error = "failed to rectify plate";
        return result;
    }

    const PlateText recognized =
        recognizer_.recognize(plate_image, error);
    if (!error.empty()) {
        result.error = std::move(error);
        return result;
    }

    result.predicted_plate =
        validator_.normalize(recognized.text);
    result.predicted_color = recognized.color;
    result.confidence = recognized.confidence;
    std::string validation_error;
    result.valid = validator_.validate(
        PlateText{
            result.predicted_plate,
            result.predicted_color,
            result.confidence,
            recognized.character_scores},
        validation_error);

    const std::string expected =
        validator_.normalize(sample.expected_plate);
    result.exact_match =
        !expected.empty() &&
        expected == result.predicted_plate &&
        (sample.expected_color == PlateColor::Unknown ||
         sample.expected_color == result.predicted_color);
    result.character_accuracy = PlateMetrics::characterAccuracy(
        expected,
        result.predicted_plate);
    result.latency_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - started)
            .count();
    return result;
}

EvaluationResult CropPlateEvaluator::evaluate(
    const EvaluationSample& sample) {
    const auto started = Clock::now();
    EvaluationResult result;
    result.sample = sample;

    std::ifstream input(sample.image_path, std::ios::binary);
    const std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const cv::Mat image = bytes.empty()
        ? cv::Mat{}
        : cv::imdecode(bytes, cv::IMREAD_COLOR);
    if (image.empty()) {
        result.error = "failed to load image: " +
            sample.image_path.string();
        return result;
    }
    result.image_loaded = true;

    std::string error;
    const PlateText recognized =
        recognizer_.recognize(image, error);
    if (!error.empty()) {
        result.error = std::move(error);
        return result;
    }

    result.predicted_plate =
        validator_.normalize(recognized.text);
    result.predicted_color = recognized.color;
    result.confidence = recognized.confidence;
    std::string validation_error;
    result.valid = validator_.validate(
        PlateText{
            result.predicted_plate,
            result.predicted_color,
            result.confidence,
            recognized.character_scores},
        validation_error);

    const std::string expected =
        validator_.normalize(sample.expected_plate);
    result.exact_match =
        !expected.empty() &&
        expected == result.predicted_plate &&
        (sample.expected_color == PlateColor::Unknown ||
         sample.expected_color == result.predicted_color);
    result.character_accuracy = PlateMetrics::characterAccuracy(
        expected,
        result.predicted_plate);
    result.latency_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - started)
            .count();
    return result;
}

}  // namespace platevision

