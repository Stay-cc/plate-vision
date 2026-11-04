#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace platevision {

class SnapshotWriter final {
public:
    explicit SnapshotWriter(std::filesystem::path root_directory);

    bool write(
        std::uint64_t stream_id,
        std::int64_t captured_at_us,
        const std::string& plate_number,
        const cv::Mat& image,
        std::filesystem::path& relative_path,
        std::string& error) const;

private:
    std::filesystem::path root_directory_;
};

}  // namespace platevision

