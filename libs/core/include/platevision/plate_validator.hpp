#pragma once

#include "platevision/types.hpp"

#include <string>

namespace platevision {

class PlateValidator final {
public:
    bool validate(
        const PlateText& plate,
        std::string& reason) const;

    std::string normalize(const std::string& text) const;

private:
    static bool isProvincePrefix(const std::string& text);
    static bool isAsciiLetter(char value);
    static bool isAsciiAlphaNumeric(char value);
    static std::size_t utf8CodepointCount(const std::string& text);
};

}  // namespace platevision

