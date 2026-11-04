#include "platevision/simple_iou_tracker.hpp"

#include <algorithm>

namespace platevision {

SimpleIouTracker::SimpleIouTracker(SimpleTrackerConfig config)
    : config_(config) {}

PlateTrack SimpleIouTracker::update(
    const PlateObservation& observation) {
    if (observation.timestamp_us != last_update_timestamp_us_) {
        for (TrackState& state : tracks_) {
            ++state.track.missed_count;
        }
        tracks_.erase(
            std::remove_if(
                tracks_.begin(),
                tracks_.end(),
                [this](const TrackState& state) {
                    return state.track.missed_count > config_.max_missed;
                }),
            tracks_.end());
        last_update_timestamp_us_ = observation.timestamp_us;
    }

    TrackState* best_track = nullptr;
    float best_iou = config_.iou_threshold;

    for (TrackState& state : tracks_) {
        const float iou =
            intersectionOverUnion(state.bounds, observation.box.bounds);
        if (iou >= best_iou) {
            best_iou = iou;
            best_track = &state;
        }
    }

    if (best_track == nullptr) {
        TrackState state;
        state.track.track_id = next_track_id_++;
        state.track.first_seen_us = observation.timestamp_us;
        state.track.confirmed = false;
        state.bounds = observation.box.bounds;
        tracks_.push_back(state);
        best_track = &tracks_.back();
    }

    best_track->bounds = observation.box.bounds;
    best_track->track.stable_text = observation.text;
    best_track->track.color = observation.color;
    best_track->track.confidence = observation.text_confidence;
    best_track->track.last_seen_us = observation.timestamp_us;
    best_track->track.missed_count = 0;
    ++best_track->track.hit_count;
    best_track->track.confirmed =
        best_track->track.hit_count >= config_.confirm_hits;

    return best_track->track;
}

void SimpleIouTracker::reset() {
    next_track_id_ = 1;
    last_update_timestamp_us_ = 0;
    tracks_.clear();
}

float SimpleIouTracker::intersectionOverUnion(
    const cv::Rect2f& left,
    const cv::Rect2f& right) {
    const float intersection_width = std::max(
        0.0f,
        std::min(left.x + left.width, right.x + right.width) -
            std::max(left.x, right.x));
    const float intersection_height = std::max(
        0.0f,
        std::min(left.y + left.height, right.y + right.height) -
            std::max(left.y, right.y));
    const float intersection = intersection_width * intersection_height;
    const float union_area =
        left.width * left.height + right.width * right.height -
        intersection;
    return union_area <= 0.0f ? 0.0f : intersection / union_area;
}

}  // namespace platevision

