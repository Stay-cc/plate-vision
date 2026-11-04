#pragma once

#include "platevision/bounded_blocking_queue.hpp"
#include "platevision/interfaces.hpp"
#include "platevision/storage/snapshot_writer.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platevision {

struct RecordTask {
    RecognitionEvent event;
    cv::Mat snapshot;
};

class RecordService final {
public:
    using ErrorHandler = std::function<void(const std::string&)>;

    RecordService(
        std::unique_ptr<IRecognitionRepository> repository,
        std::unique_ptr<SnapshotWriter> snapshot_writer,
        ErrorHandler error_handler = {});
    ~RecordService();

    bool start(std::string& error);
    bool enqueue(
        RecordTask task,
        std::chrono::milliseconds timeout =
            std::chrono::milliseconds(250));
    void flushAndStop();

    std::vector<RecognitionEvent> query(
        const RecordQuery& query,
        std::string& error);

private:
    void writerLoop();
    void processTask(RecordTask task);

    std::unique_ptr<IRecognitionRepository> repository_;
    std::unique_ptr<SnapshotWriter> snapshot_writer_;
    ErrorHandler error_handler_;
    BoundedBlockingQueue<RecordTask> queue_{128};
    std::thread writer_thread_;
    std::atomic<bool> running_{false};
    std::mutex repository_mutex_;
};

}  // namespace platevision

