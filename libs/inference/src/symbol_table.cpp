#include "platevision/inference/symbol_table.hpp"

#include <fstream>
#include <iterator>
#include <sstream>

namespace platevision {
namespace {

std::string trimCarriageReturn(std::string value) {
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return value;
}

}  // namespace

bool SymbolTable::loadFromFile(
    const std::filesystem::path& path,
    SymbolTable& table,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "failed to open symbol table: " + path.string();
        return false;
    }

    std::vector<std::string> symbols;
    std::string line;
    while (std::getline(input, line)) {
        line = trimCarriageReturn(std::move(line));
        if (line.empty()) {
            continue;
        }
        symbols.push_back(std::move(line));
    }

    if (symbols.size() < 2 || symbols.front() != "<blank>") {
        error = "symbol table must start with <blank> and contain tokens";
        return false;
    }

    table.symbols_ = std::move(symbols);
    error.clear();
    return true;
}

SymbolTable SymbolTable::defaultPlateAlphabet() {
    SymbolTable table;
    table.symbols_ = {
        "<blank>", "京", "津", "冀", "晋", "蒙", "辽", "吉", "黑",
        "沪", "苏", "浙", "皖", "闽", "赣", "鲁", "豫", "鄂", "湘",
        "粤", "桂", "琼", "渝", "川", "贵", "云", "藏", "陕", "甘",
        "青", "宁", "新", "0", "1", "2", "3", "4", "5", "6", "7",
        "8", "9", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
        "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
        "W", "X", "Y", "Z"};
    return table;
}

std::size_t SymbolTable::size() const noexcept {
    return symbols_.size();
}

const std::string& SymbolTable::token(std::size_t index) const {
    return symbols_.at(index);
}

bool SymbolTable::empty() const noexcept {
    return symbols_.empty();
}

}  // namespace platevision

