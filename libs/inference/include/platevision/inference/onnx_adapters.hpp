#pragma once

#include "platevision/inference/symbol_table.hpp"
#include "platevision/interfaces.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace platevision {

enum class ExecutionProvider {
    Cpu,
    DirectML
};

enum class DetectionOutputLayout {
    ChannelsFirst,
    DetectionsFirst
};

struct OnnxDetectorConfig {
    std::filesystem::path model_path;
    DetectionOutputLayout output_layout{
        DetectionOutputLayout::ChannelsFirst};
    int input_width{0};
    int input_height{0};
    int intra_threads{1};
    ExecutionProvider provider{ExecutionProvider::Cpu};
    int device_id{0};
    float score_threshold{0.35f};
    float iou_threshold{0.45f};
    std::size_t max_detections{32};
    bool coordinates_normalized{false};
};

enum class RecognitionOutputLayout {
    TimeMajor,
    ClassMajor
};

struct OnnxRecognizerConfig {
    std::filesystem::path model_path;
    SymbolTable symbols;
    RecognitionOutputLayout output_layout{
        RecognitionOutputLayout::TimeMajor};
    int input_width{0};
    int input_height{0};
    int intra_threads{1};
    ExecutionProvider provider{ExecutionProvider::Cpu};
    int device_id{0};
    int blank_index{0};
};

struct HyperLprDetectorConfig {
    std::filesystem::path model_path;
    int intra_threads{1};
    ExecutionProvider provider{ExecutionProvider::Cpu};
    int device_id{0};
    float score_threshold{0.30f};
    float iou_threshold{0.60f};
    std::size_t max_detections{32};
};

struct HyperLprRecognizerConfig {
    std::filesystem::path model_path;
    std::filesystem::path classifier_model_path;
    SymbolTable symbols;
    int intra_threads{1};
    ExecutionProvider provider{ExecutionProvider::Cpu};
    int device_id{0};
};

std::unique_ptr<IPlateDetector> createOnnxPlateDetector(
    const OnnxDetectorConfig& config,
    std::string& error);

std::unique_ptr<IPlateRecognizer> createOnnxPlateRecognizer(
    const OnnxRecognizerConfig& config,
    std::string& error);

std::unique_ptr<IPlateDetector> createHyperLprPlateDetector(
    const HyperLprDetectorConfig& config,
    std::string& error);

std::unique_ptr<IPlateRecognizer> createHyperLprPlateRecognizer(
    const HyperLprRecognizerConfig& config,
    std::string& error);

}  // namespace platevision

