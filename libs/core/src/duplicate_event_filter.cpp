#include "platevision/duplicate_event_filter.hpp"

#include "platevision/plate_validator.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace platevision {

DuplicateEventFilter::DuplicateEventFilter(
    std::chrono::milliseconds cooldown,
    std::size_t max_entries)
    : cooldown_(cooldown), max_entries_(std::max<std::size_t>(1, max_entries)) {}

bool DuplicateEventFilter::shouldEmit(
    std::uint64_t camera_id,
    const std::string& normalized_plate,
    std::int64_t now_us) {
    clearExpired(now_us);
    const Key key{camera_id, PlateValidator{}.normalize(normalized_plate)};
    const auto iterator = last_emitted_.find(key);
    const std::int64_t cooldown_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            cooldown_)
            .count();
    if (iterator != last_emitted_.end() &&
        now_us - iterator->second < cooldown_us) {
        return false;
    }
    last_emitted_[key] = now_us;
    if (last_emitted_.size() > max_entries_) {
        clearExpired(now_us);
        if (last_emitted_.size() > max_entries_) {
            last_emitted_.clear();
            last_emitted_[key] = now_us;
        }
    }
    return true;
}

void DuplicateEventFilter::clearExpired(std::int64_t now_us) {
    const std::int64_t cooldown_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            cooldown_)
            .count();
    for (auto iterator = last_emitted_.begin();
         iterator != last_emitted_.end();) {
        if (now_us - iterator->second >= cooldown_us) {
            iterator = last_emitted_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void DuplicateEventFilter::reset() {
    last_emitted_.clear();
}

std::size_t DuplicateEventFilter::KeyHash::operator()(
    const Key& key) const noexcept {
    const std::size_t first = std::hash<std::uint64_t>{}(key.camera_id);
    const std::size_t second = std::hash<std::string>{}(key.plate);
    return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
}

}  // namespace platevision

