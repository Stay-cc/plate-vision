#include "platevision/plate_validator.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string_view>

namespace platevision {
namespace {

constexpr std::array<std::string_view, 31> kProvincePrefixes = {
    "京", "津", "冀", "晋", "蒙", "辽", "吉", "黑", "沪", "苏",
    "浙", "皖", "闽", "赣", "鲁", "豫", "鄂", "湘", "粤", "桂",
    "琼", "渝", "川", "贵", "云", "藏", "陕", "甘", "青", "宁",
    "新"};

}  // namespace

bool PlateValidator::validate(
    const PlateText& plate,
    std::string& reason) const {
    const std::string normalized = normalize(plate.text);
    const std::size_t length = utf8CodepointCount(normalized);

    if (length != 7 && length != 8) {
        reason = "plate number must contain 7 or 8 characters";
        return false;
    }

    if (normalized.size() < 4 || !isProvincePrefix(normalized.substr(0, 3))) {
        reason = "plate number has an unsupported province prefix";
        return false;
    }

    if (!isAsciiLetter(normalized[3])) {
        reason = "plate number has an invalid city letter";
        return false;
    }

    for (std::size_t index = 4; index < normalized.size(); ++index) {
        if (!isAsciiAlphaNumeric(normalized[index])) {
            reason = "plate number contains unsupported characters";
            return false;
        }
    }

    if (plate.color == PlateColor::Unknown) {
        reason = "plate color is required";
        return false;
    }

    if (plate.color == PlateColor::Blue && length != 7) {
        reason = "blue plates must contain 7 characters";
        return false;
    }

    if (plate.color == PlateColor::Green && length != 8) {
        reason = "green plates must contain 8 characters";
        return false;
    }

    reason.clear();
    return true;
}

std::string PlateValidator::normalize(const std::string& text) const {
    std::string normalized;
    normalized.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index) {
        const unsigned char value =
            static_cast<unsigned char>(text[index]);

        if (value == ' ' || value == '\t' || value == '\r' ||
            value == '\n' || value == '-') {
            continue;
        }

        if (value == 0xC2 && index + 1 < text.size() &&
            static_cast<unsigned char>(text[index + 1]) == 0xB7) {
            ++index;
            continue;
        }

        normalized.push_back(static_cast<char>(value));
    }

    return normalized;
}

bool PlateValidator::isProvincePrefix(const std::string& text) {
    const std::string_view candidate(text);
    return std::find(
               kProvincePrefixes.begin(),
               kProvincePrefixes.end(),
               candidate) != kProvincePrefixes.end();
}

bool PlateValidator::isAsciiLetter(char value) {
    const unsigned char converted =
        static_cast<unsigned char>(value);
    return std::isalpha(converted) != 0;
}

bool PlateValidator::isAsciiAlphaNumeric(char value) {
    const unsigned char converted =
        static_cast<unsigned char>(value);
    return std::isalnum(converted) != 0;
}

std::size_t PlateValidator::utf8CodepointCount(
    const std::string& text) {
    std::size_t count = 0;
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

        index += std::min(width, text.size() - index);
        ++count;
    }

    return count;
}

}  // namespace platevision

