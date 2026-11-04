#include "platevision/evaluation/plate_metrics.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace platevision {
namespace {

std::vector<std::string> splitUtf8(const std::string& text) {
    std::vector<std::string> characters;
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char value =
            static_cast<unsigned char>(text[index]);
        std::size_t width = 1;
        if ((value & 0x80U) == 0) {
            width = 1;
        } else if ((value & 0xE0U) == 0xC0U) {
            width = 2;
        } else if ((value & 0xF0U) == 0xE0U) {
            width = 3;
        } else if ((value & 0xF8U) == 0xF0U) {
            width = 4;
        }
        width = std::min(width, text.size() - index);
        characters.push_back(text.substr(index, width));
        index += width;
    }
    return characters;
}

}  // namespace

std::size_t PlateMetrics::utf8Length(const std::string& text) {
    return splitUtf8(text).size();
}

std::size_t PlateMetrics::editDistance(
    const std::string& left,
    const std::string& right) {
    const std::vector<std::string> left_chars = splitUtf8(left);
    const std::vector<std::string> right_chars = splitUtf8(right);

    std::vector<std::size_t> previous(right_chars.size() + 1);
    std::vector<std::size_t> current(right_chars.size() + 1);
    for (std::size_t index = 0; index <= right_chars.size(); ++index) {
        previous[index] = index;
    }

    for (std::size_t left_index = 1;
         left_index <= left_chars.size();
         ++left_index) {
        current[0] = left_index;
        for (std::size_t right_index = 1;
             right_index <= right_chars.size();
             ++right_index) {
            const std::size_t substitution =
                previous[right_index - 1] +
                (left_chars[left_index - 1] ==
                         right_chars[right_index - 1]
                     ? 0
                     : 1);
            current[right_index] = std::min({
                previous[right_index] + 1,
                current[right_index - 1] + 1,
                substitution});
        }
        std::swap(previous, current);
    }
    return previous.back();
}

double PlateMetrics::characterAccuracy(
    const std::string& expected,
    const std::string& predicted) {
    const std::size_t length = std::max(
        utf8Length(expected),
        utf8Length(predicted));
    if (length == 0) {
        return 1.0;
    }
    const std::size_t distance = editDistance(expected, predicted);
    return std::max(
        0.0,
        1.0 - static_cast<double>(distance) /
                  static_cast<double>(length));
}

float PlateMetrics::intersectionOverUnion(
    float left_x,
    float left_y,
    float left_width,
    float left_height,
    float right_x,
    float right_y,
    float right_width,
    float right_height) {
    const float intersection_width = std::max(
        0.0f,
        std::min(left_x + left_width, right_x + right_width) -
            std::max(left_x, right_x));
    const float intersection_height = std::max(
        0.0f,
        std::min(left_y + left_height, right_y + right_height) -
            std::max(left_y, right_y));
    const float intersection = intersection_width * intersection_height;
    const float union_area =
        left_width * left_height + right_width * right_height -
        intersection;
    return union_area <= 0.0f ? 0.0f : intersection / union_area;
}

}  // namespace platevision

