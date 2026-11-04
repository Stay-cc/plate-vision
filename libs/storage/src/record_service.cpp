#include "platevision/storage/record_service.hpp"

#include <filesystem>
#include <utility>

namespace platevision {

RecordService::RecordService(
    std::unique_ptr<IRecognitionRepository> repository,
    std::unique_ptr<SnapshotWriter> snapshot_writer,
    ErrorHandler error_handler)
    : repository_(std::move(repository)),
      snapshot_writer_(std::move(snapshot_writer)),
      error_handler_(std::move(error_handler)) {}

RecordService::~RecordService() {
    flushAndStop();
}

bool RecordService::start(std::string& error) {
    if (running_.load()) {
        error = "record service is already running";
        return false;
    }
    if (!repository_ || !snapshot_writer_) {
        error = "record service dependencies are missing";
        return false;
    }
    if (!repository_->initialize(error)) {
        return false;
    }
    running_.store(true);
    writer_thread_ = std::thread(&RecordService::writerLoop, this);
    return true;
}

bool RecordService::enqueue(
    RecordTask task,
    std::chrono::milliseconds timeout) {
    return queue_.push(std::move(task), timeout);
}

void RecordService::flushAndStop() {
    queue_.close();
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
    running_.store(false);
}

std::vector<RecognitionEvent> RecordService::query(
    const RecordQuery& query,
    std::string& error) {
    std::lock_guard<std::mutex> lock(repository_mutex_);
    if (!repository_) {
        error = "record repository is missing";
        return {};
    }
    return repository_->query(query, error);
}

void RecordService::writerLoop() {
    while (!queue_.closed() || !queue_.empty()) {
        RecordTask task;
        if (!queue_.pop(task, std::chrono::milliseconds(100))) {
            continue;
        }
        processTask(std::move(task));
    }
    running_.store(false);
}

void RecordService::processTask(RecordTask task) {
    std::string error;
    if (!task.snapshot.empty()) {
        std::filesystem::path relative_path;
        if (snapshot_writer_->write(
                task.event.stream_id,
                task.event.captured_at_us,
                task.event.plate_number,
                task.snapshot,
                relative_path,
                error)) {
            task.event.snapshot_path = relative_path.u8string();
        } else if (error_handler_) {
            error_handler_(error);
        }
    }

    std::lock_guard<std::mutex> lock(repository_mutex_);
    if (!repository_->save(task.event, error) && error_handler_) {
        error_handler_(error);
    }
}

}  // namespace platevision

