#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace platevision {

class SymbolTable final {
public:
    static bool loadFromFile(
        const std::filesystem::path& path,
        SymbolTable& table,
        std::string& error);

    static SymbolTable defaultPlateAlphabet();

    std::size_t size() const noexcept;
    const std::string& token(std::size_t index) const;
    bool empty() const noexcept;

private:
    std::vector<std::string> symbols_;
};

}  // namespace platevision

