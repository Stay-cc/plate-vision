#pragma once

#include "platevision/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace platevision {

class DuplicateEventFilter final {
public:
    explicit DuplicateEventFilter(
        std::chrono::milliseconds cooldown =
            std::chrono::milliseconds(8000),
        std::size_t max_entries = 4096);

    bool shouldEmit(
        std::uint64_t camera_id,
        const std::string& normalized_plate,
        std::int64_t now_us);

    void clearExpired(std::int64_t now_us);
    void reset();

private:
    struct Key {
        std::uint64_t camera_id;
        std::string plate;

        bool operator==(const Key& other) const noexcept {
            return camera_id == other.camera_id &&
                plate == other.plate;
        }
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept;
    };

    std::chrono::milliseconds cooldown_;
    std::size_t max_entries_;
    std::unordered_map<Key, std::int64_t, KeyHash> last_emitted_;
};

}  // namespace platevision

