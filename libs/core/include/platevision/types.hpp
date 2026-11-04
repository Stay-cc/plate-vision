#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace platevision {

enum class PlateColor {
    Unknown,
    Blue,
    Green,
    Yellow
};

struct Frame {
    std::uint64_t stream_id{0};
    std::uint64_t sequence{0};
    std::int64_t source_timestamp_us{0};
    std::int64_t receive_timestamp_us{0};
    cv::Mat bgr;
};

using FrameRef = std::shared_ptr<const Frame>;

struct PlateBox {
    cv::Rect2f bounds;
    std::array<cv::Point2f, 4> quad{};
    float confidence{0.0f};
};

struct PlateText {
    std::string text;
    PlateColor color{PlateColor::Unknown};
    float confidence{0.0f};
    std::vector<float> character_scores;
};

struct PlateObservation {
    std::uint64_t stream_id{0};
    std::uint64_t frame_sequence{0};
    std::int64_t timestamp_us{0};
    PlateBox box;
    std::string text;
    PlateColor color{PlateColor::Unknown};
    float text_confidence{0.0f};
    bool valid{false};
};

struct PlateTrack {
    std::uint64_t track_id{0};
    std::string stable_text;
    PlateColor color{PlateColor::Unknown};
    float confidence{0.0f};
    int hit_count{0};
    int missed_count{0};
    std::int64_t first_seen_us{0};
    std::int64_t last_seen_us{0};
    bool confirmed{false};
};

struct StageTimings {
    std::chrono::microseconds detect{0};
    std::chrono::microseconds rectify{0};
    std::chrono::microseconds recognize{0};
    std::chrono::microseconds track{0};
    std::chrono::microseconds total{0};
};

struct FrameResult {
    FrameRef frame;
    std::vector<PlateObservation> observations;
    std::vector<PlateTrack> tracks;
    StageTimings timings;
    std::string error;
};

struct RecognitionEvent {
    std::int64_t event_id{0};
    std::uint64_t stream_id{0};
    std::uint64_t camera_id{0};
    std::uint64_t track_id{0};
    std::string plate_number;
    PlateColor color{PlateColor::Unknown};
    float confidence{0.0f};
    std::int64_t captured_at_us{0};
    std::string snapshot_path;
    cv::Rect2f bounding_box;
};

struct RecordQuery {
    std::optional<std::uint64_t> camera_id;
    std::optional<std::string> plate_number;
    std::int64_t begin_ms{0};
    std::int64_t end_ms{0};
    int limit{200};
    int offset{0};
};

inline const char* toString(PlateColor color) noexcept {
    switch (color) {
    case PlateColor::Blue:
        return "blue";
    case PlateColor::Green:
        return "green";
    case PlateColor::Yellow:
        return "yellow";
    case PlateColor::Unknown:
    default:
        return "unknown";
    }
}

}  // namespace platevision

