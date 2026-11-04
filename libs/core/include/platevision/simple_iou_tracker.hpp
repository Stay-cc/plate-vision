#pragma once

#include "platevision/interfaces.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace platevision {

struct SimpleTrackerConfig {
    float iou_threshold{0.30f};
    int confirm_hits{3};
    int max_missed{30};
};

class SimpleIouTracker final : public IPlateTracker {
public:
    explicit SimpleIouTracker(SimpleTrackerConfig config = {});

    PlateTrack update(const PlateObservation& observation) override;
    void reset() override;

private:
    struct TrackState {
        PlateTrack track;
        cv::Rect2f bounds;
    };

    static float intersectionOverUnion(
        const cv::Rect2f& left,
        const cv::Rect2f& right);

    SimpleTrackerConfig config_;
    std::uint64_t next_track_id_{1};
    std::int64_t last_update_timestamp_us_{0};
    std::vector<TrackState> tracks_;
};

}  // namespace platevision

