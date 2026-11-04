#include "platevision/video/sample_generator.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <filesystem>

namespace platevision {

bool generateSampleVideo(
    const std::filesystem::path& output_path,
    int width,
    int height,
    int frame_count,
    double fps,
    std::string& error) {
    if (width < 320 || height < 240 || frame_count <= 0 || fps <= 0.0) {
        error = "invalid sample video dimensions or frame count";
        return false;
    }

    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    cv::VideoWriter writer;
    const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    if (!writer.open(
            output_path.string(),
            fourcc,
            fps,
            cv::Size(width, height))) {
        error = "failed to create sample video: " + output_path.string();
        return false;
    }

    for (int index = 0; index < frame_count; ++index) {
        cv::Mat frame(height, width, CV_8UC3);
        frame.setTo(cv::Scalar(42, 47, 54));

        const int horizon = static_cast<int>(height * 0.52);
        cv::rectangle(
            frame,
            cv::Rect(0, horizon, width, height - horizon),
            cv::Scalar(74, 79, 86),
            cv::FILLED);

        const int lane_offset = static_cast<int>(
            (index * 11) % std::max(1, width));
        for (int lane = -width; lane < width * 2; lane += 220) {
            const int x = lane + lane_offset;
            cv::rectangle(
                frame,
                cv::Rect(x, horizon + 80, 120, 12),
                cv::Scalar(200, 205, 210),
                cv::FILLED);
        }

        const int plate_width = static_cast<int>(width * 0.24);
        const int plate_height = static_cast<int>(height * 0.075);
        const int plate_left = (width - plate_width) / 2;
        const int plate_top = static_cast<int>(height * 0.56);
        const cv::Rect plate_rect(
            plate_left, plate_top, plate_width, plate_height);

        cv::rectangle(
            frame,
            cv::Rect(
                plate_left - 180,
                plate_top - 260,
                plate_width + 360,
                plate_height + 330),
            cv::Scalar(25, 55, 110),
            cv::FILLED);

        cv::rectangle(
            frame,
            plate_rect,
            cv::Scalar(245, 245, 245),
            cv::FILLED);
        cv::rectangle(
            frame,
            plate_rect,
            cv::Scalar(10, 10, 10),
            3);

        cv::putText(
            frame,
            "A12345",
            cv::Point(plate_left + plate_width / 6,
                      plate_top + plate_height * 3 / 4),
            cv::FONT_HERSHEY_SIMPLEX,
            plate_height / 55.0,
            cv::Scalar(20, 20, 20),
            3,
            cv::LINE_AA);

        cv::putText(
            frame,
            "SYNTHETIC TEST VIDEO",
            cv::Point(40, 70),
            cv::FONT_HERSHEY_SIMPLEX,
            1.1,
            cv::Scalar(230, 235, 240),
            2,
            cv::LINE_AA);

        writer.write(frame);
    }

    writer.release();
    error.clear();
    return true;
}

}  // namespace platevision

