#include "platevision/event_coordinator.hpp"

#include <algorithm>
#include <utility>

namespace platevision {

EventCoordinator::EventCoordinator(EventCoordinatorConfig config)
    : config_(std::move(config)),
      voter_(config_.voter),
      duplicate_filter_(config_.duplicate_cooldown) {}

std::vector<RecognitionEvent> EventCoordinator::process(
    const FrameResult& result) {
    std::vector<RecognitionEvent> events;
    if (!result.frame) {
        return events;
    }

    voter_.removeExpired(result.frame->source_timestamp_us);
    for (std::size_t index = 0;
         index < result.observations.size() &&
         index < result.tracks.size();
         ++index) {
        const PlateObservation& observation = result.observations[index];
        const PlateTrack& track = result.tracks[index];
        if (!observation.valid) {
            continue;
        }

        const VoteResult vote = voter_.update(
            track.track_id,
            observation.text,
            observation.color,
            observation.text_confidence,
            observation.timestamp_us);
        if (!track.confirmed || !vote.stable) {
            continue;
        }
        const std::int64_t minimum_duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                config_.min_track_duration)
                .count();
        if (observation.timestamp_us - track.first_seen_us <
            minimum_duration_us) {
            continue;
        }
        if (emitted_tracks_.count(track.track_id) > 0) {
            continue;
        }
        if (!duplicate_filter_.shouldEmit(
                config_.camera_id,
                vote.text,
                observation.timestamp_us)) {
            continue;
        }

        RecognitionEvent event;
        event.stream_id = observation.stream_id;
        event.camera_id = config_.camera_id;
        event.track_id = track.track_id;
        event.plate_number = vote.text;
        event.color =
            vote.color == PlateColor::Unknown
                ? observation.color
                : vote.color;
        event.confidence =
            vote.confidence > 0.0f
                ? vote.confidence
                : observation.text_confidence;
        event.captured_at_us =
            result.frame->receive_timestamp_us > 0
                ? result.frame->receive_timestamp_us
                : observation.timestamp_us;
        event.bounding_box = observation.box.bounds;
        emitted_tracks_.insert(track.track_id);
        events.push_back(std::move(event));
    }
    return events;
}

void EventCoordinator::reset() {
    voter_.reset();
    duplicate_filter_.reset();
    emitted_tracks_.clear();
}

}  // namespace platevision

