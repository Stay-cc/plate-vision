#include "platevision/stable_text_voter.hpp"

#include "platevision/plate_validator.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>

namespace platevision {

StableTextVoter::StableTextVoter(StableTextVoterConfig config)
    : config_(std::move(config)) {
    if (config_.window_size == 0) {
        config_.window_size = 1;
    }
    config_.stable_hits = std::max(1, config_.stable_hits);
}

VoteResult StableTextVoter::update(
    std::uint64_t track_id,
    const std::string& text,
    PlateColor color,
    float confidence,
    std::int64_t timestamp_us) {
    removeExpired(timestamp_us);

    TrackVotes& state = tracks_[track_id];
    state.last_seen_us = timestamp_us;
    state.votes.push_back(
        Vote{text, color, confidence, timestamp_us});
    while (state.votes.size() > config_.window_size) {
        state.votes.pop_front();
    }

    std::unordered_map<std::string, float> weights;
    std::unordered_map<std::string, int> counts;
    std::unordered_map<std::string, float> color_blue;
    std::unordered_map<std::string, float> color_green;
    std::unordered_map<std::string, float> color_yellow;
    float total_weight = 0.0f;
    for (const Vote& vote : state.votes) {
        const float weight = std::max(0.01f, vote.confidence);
        weights[vote.text] += weight;
        ++counts[vote.text];
        total_weight += weight;
        if (vote.color == PlateColor::Blue) {
            color_blue[vote.text] += weight;
        } else if (vote.color == PlateColor::Green) {
            color_green[vote.text] += weight;
        } else if (vote.color == PlateColor::Yellow) {
            color_yellow[vote.text] += weight;
        }
    }

    std::string best_text;
    float best_weight = -std::numeric_limits<float>::infinity();
    for (const auto& entry : weights) {
        if (entry.second > best_weight) {
            best_weight = entry.second;
            best_text = entry.first;
        }
    }

    VoteResult result;
    result.text = best_text;
    result.hit_count = counts[best_text];
    result.confidence = counts[best_text] > 0
        ? best_weight / static_cast<float>(counts[best_text])
        : 0.0f;
    const float best_blue = color_blue[best_text];
    const float best_green = color_green[best_text];
    const float best_yellow = color_yellow[best_text];
    if (best_yellow > best_blue && best_yellow > best_green) {
        result.color = PlateColor::Yellow;
    } else if (best_green > best_blue) {
        result.color = PlateColor::Green;
    } else if (best_blue > 0.0f) {
        result.color = PlateColor::Blue;
    } else {
        result.color = PlateColor::Unknown;
    }
    result.stable =
        static_cast<int>(state.votes.size()) >= config_.stable_hits &&
        result.hit_count >= config_.stable_hits &&
        total_weight > 0.0f &&
        best_weight / total_weight >=
            static_cast<float>(config_.stable_ratio);
    return result;
}

void StableTextVoter::removeExpired(std::int64_t now_us) {
    const std::int64_t ttl_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            config_.track_ttl)
            .count();
    for (auto iterator = tracks_.begin(); iterator != tracks_.end();) {
        if (now_us - iterator->second.last_seen_us > ttl_us) {
            iterator = tracks_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void StableTextVoter::reset() {
    tracks_.clear();
}

}  // namespace platevision

