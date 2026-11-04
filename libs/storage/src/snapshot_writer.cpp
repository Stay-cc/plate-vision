#include "platevision/storage/snapshot_writer.hpp"

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace platevision {
namespace {

std::string sanitizeFileName(const std::string& plate) {
    std::string result;
    for (const unsigned char character : plate) {
        if ((character >= '0' && character <= '9') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            character == '-' || character == '_') {
            result.push_back(static_cast<char>(character));
        }
    }
    return result.empty() ? "plate" : result;
}

bool localTime(
    std::int64_t timestamp_us,
    std::tm& time,
    std::string& error) {
    const std::time_t seconds = static_cast<std::time_t>(
        timestamp_us / 1000000);
#ifdef _WIN32
    if (localtime_s(&time, &seconds) != 0) {
        error = "failed to convert snapshot timestamp";
        return false;
    }
#else
    if (localtime_r(&seconds, &time) == nullptr) {
        error = "failed to convert snapshot timestamp";
        return false;
    }
#endif
    return true;
}

}  // namespace

SnapshotWriter::SnapshotWriter(std::filesystem::path root_directory)
    : root_directory_(std::move(root_directory)) {}

bool SnapshotWriter::write(
    std::uint64_t stream_id,
    std::int64_t captured_at_us,
    const std::string& plate_number,
    const cv::Mat& image,
    std::filesystem::path& relative_path,
    std::string& error) const {
    if (image.empty()) {
        error = "snapshot image is empty";
        return false;
    }

    std::tm time{};
    if (!localTime(captured_at_us, time, error)) {
        return false;
    }

    std::ostringstream directory;
    directory << std::setfill('0')
              << std::setw(4) << time.tm_year + 1900 << "/"
              << std::setw(2) << time.tm_mon + 1 << "/"
              << std::setw(2) << time.tm_mday << "/"
              << stream_id;
    const std::filesystem::path absolute_directory =
        root_directory_ / directory.str();

    std::error_code filesystem_error;
    std::filesystem::create_directories(
        absolute_directory,
        filesystem_error);
    if (filesystem_error) {
        error = "failed to create snapshot directory: " +
            filesystem_error.message();
        return false;
    }

    std::ostringstream file_name;
    file_name << captured_at_us << "_"
              << sanitizeFileName(plate_number) << ".jpg";
    const std::filesystem::path final_path =
        absolute_directory / file_name.str();
    const std::filesystem::path temporary_path =
        final_path.string() + ".tmp";

    std::vector<unsigned char> encoded;
    if (!cv::imencode(".jpg", image, encoded)) {
        error = "failed to encode snapshot";
        return false;
    }

    {
        std::ofstream output(temporary_path, std::ios::binary);
        if (!output) {
            error = "failed to open temporary snapshot";
            return false;
        }
        output.write(
            reinterpret_cast<const char*>(encoded.data()),
            static_cast<std::streamsize>(encoded.size()));
        if (!output) {
            error = "failed to write snapshot";
            return false;
        }
    }

    std::filesystem::rename(
        temporary_path,
        final_path,
        filesystem_error);
    if (filesystem_error) {
        std::filesystem::remove(temporary_path);
        error = "failed to finalize snapshot: " +
            filesystem_error.message();
        return false;
    }

    relative_path = std::filesystem::relative(
        final_path,
        root_directory_,
        filesystem_error);
    if (filesystem_error) {
        relative_path = final_path;
    }
    error.clear();
    return true;
}

}  // namespace platevision

