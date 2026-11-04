#pragma once

#include "platevision/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

namespace platevision {

struct StableTextVoterConfig {
    std::size_t window_size{10};
    int stable_hits{6};
    double stable_ratio{0.70};
    std::chrono::milliseconds track_ttl{2000};
};

struct VoteResult {
    bool stable{false};
    std::string text;
    PlateColor color{PlateColor::Unknown};
    float confidence{0.0f};
    int hit_count{0};
};

class StableTextVoter final {
public:
    explicit StableTextVoter(StableTextVoterConfig config = {});

    VoteResult update(
        std::uint64_t track_id,
        const std::string& text,
        PlateColor color,
        float confidence,
        std::int64_t timestamp_us);

    void removeExpired(std::int64_t now_us);
    void reset();

private:
    struct Vote {
        std::string text;
        PlateColor color;
        float confidence;
        std::int64_t timestamp_us;
    };

    struct TrackVotes {
        std::deque<Vote> votes;
        std::int64_t last_seen_us{0};
    };

    StableTextVoterConfig config_;
    std::unordered_map<std::uint64_t, TrackVotes> tracks_;
};

}  // namespace platevision

