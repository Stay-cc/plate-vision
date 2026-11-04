#pragma once

#include "platevision/duplicate_event_filter.hpp"
#include "platevision/stable_text_voter.hpp"
#include "platevision/types.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace platevision {

struct EventCoordinatorConfig {
    std::uint64_t camera_id{1};
    StableTextVoterConfig voter;
    std::chrono::milliseconds duplicate_cooldown{30000};
    std::chrono::milliseconds min_track_duration{800};
};

class EventCoordinator final {
public:
    explicit EventCoordinator(EventCoordinatorConfig config = {});

    std::vector<RecognitionEvent> process(const FrameResult& result);
    void reset();

private:
    EventCoordinatorConfig config_;
    StableTextVoter voter_;
    DuplicateEventFilter duplicate_filter_;
    std::unordered_set<std::uint64_t> emitted_tracks_;
};

}  // namespace platevision

