#include "platevision/recognition_pipeline.hpp"

#include <chrono>
#include <utility>

namespace platevision {
namespace {

using Clock = std::chrono::steady_clock;

std::chrono::microseconds elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - start);
}

}  // namespace

RecognitionPipeline::RecognitionPipeline(
    IPlateDetector& detector,
    IPlateRectifier& rectifier,
    IPlateRecognizer& recognizer,
    IPlateTracker& tracker)
    : detector_(detector),
      rectifier_(rectifier),
      recognizer_(recognizer),
      tracker_(tracker) {}

FrameResult RecognitionPipeline::process(FrameRef frame) {
    FrameResult result;
    result.frame = std::move(frame);

    const auto total_start = Clock::now();
    if (!result.frame || result.frame->bgr.empty()) {
        result.error = "input frame is empty";
        return result;
    }

    std::string error;
    const auto detect_start = Clock::now();
    const std::vector<PlateBox> boxes =
        detector_.detect(*result.frame, error);
    result.timings.detect = elapsed(detect_start);
    if (!error.empty()) {
        result.error = error;
        result.timings.total = elapsed(total_start);
        return result;
    }

    for (const PlateBox& box : boxes) {
        const auto rectify_start = Clock::now();
        const cv::Mat plate_image = rectifier_.rectify(*result.frame, box);
        result.timings.rectify += elapsed(rectify_start);
        if (plate_image.empty()) {
            continue;
        }

        error.clear();
        const auto recognize_start = Clock::now();
        PlateText plate_text =
            recognizer_.recognize(plate_image, error);
        result.timings.recognize += elapsed(recognize_start);
        if (!error.empty()) {
            continue;
        }

        PlateObservation observation;
        observation.stream_id = result.frame->stream_id;
        observation.frame_sequence = result.frame->sequence;
        observation.timestamp_us = result.frame->source_timestamp_us;
        observation.box = box;
        observation.text = validator_.normalize(plate_text.text);
        observation.color = plate_text.color;
        observation.text_confidence = plate_text.confidence;

        std::string validation_error;
        observation.valid =
            validator_.validate(plate_text, validation_error);

        const auto track_start = Clock::now();
        const PlateTrack track = tracker_.update(observation);
        result.timings.track += elapsed(track_start);

        result.observations.push_back(observation);
        result.tracks.push_back(track);
    }

    result.timings.total = elapsed(total_start);
    return result;
}

void RecognitionPipeline::reset() {
    tracker_.reset();
}

}  // namespace platevision

