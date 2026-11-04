#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

namespace platevision {

template <typename T>
class BoundedLatestQueue final {
public:
    explicit BoundedLatestQueue(std::size_t capacity)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    bool pushLatest(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return false;
            }

            if (items_.size() >= capacity_) {
                items_.pop_front();
                ++dropped_;
            }

            items_.push_back(std::move(value));
        }
        condition_.notify_one();
        return true;
    }

    bool pop(T& value, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = condition_.wait_for(
            lock,
            timeout,
            [this] { return closed_ || !items_.empty(); });

        if (!ready || items_.empty()) {
            return false;
        }

        value = std::move(items_.front());
        items_.pop_front();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.empty();
    }

    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    std::uint64_t droppedCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    std::size_t capacity_;
    std::deque<T> items_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool closed_{false};
    std::uint64_t dropped_{0};
};

}  // namespace platevision

