#pragma once

#include <string>

namespace platevision {

class PlateMetrics final {
public:
    static std::size_t utf8Length(const std::string& text);
    static std::size_t editDistance(
        const std::string& left,
        const std::string& right);
    static double characterAccuracy(
        const std::string& expected,
        const std::string& predicted);
    static float intersectionOverUnion(
        float left_x,
        float left_y,
        float left_width,
        float left_height,
        float right_x,
        float right_y,
        float right_width,
        float right_height);
};

}  // namespace platevision

