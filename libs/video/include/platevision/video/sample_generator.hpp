#pragma once

#include <filesystem>
#include <string>

namespace platevision {

bool generateSampleVideo(
    const std::filesystem::path& output_path,
    int width,
    int height,
    int frame_count,
    double fps,
    std::string& error);

}  // namespace platevision

