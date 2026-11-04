#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace platevision {

template <typename T>
class BoundedBlockingQueue final {
public:
    explicit BoundedBlockingQueue(std::size_t capacity)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    bool push(T value, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = not_full_.wait_for(
            lock,
            timeout,
            [this] { return closed_ || items_.size() < capacity_; });
        if (!ready || closed_) {
            return false;
        }
        items_.push_back(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    bool pop(T& value, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = not_empty_.wait_for(
            lock,
            timeout,
            [this] { return closed_ || !items_.empty(); });
        if (!ready || items_.empty()) {
            return false;
        }
        value = std::move(items_.front());
        items_.pop_front();
        not_full_.notify_one();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.empty();
    }

    bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    std::size_t capacity_;
    std::deque<T> items_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    bool closed_{false};
};

}  // namespace platevision

