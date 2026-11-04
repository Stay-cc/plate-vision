#include "platevision/evaluation/plate_evaluator.hpp"
#include "platevision/evaluation/plate_metrics.hpp"
#include "platevision/inference/onnx_adapters.hpp"
#include "platevision/inference/symbol_table.hpp"
#include "platevision/mock_adapters.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace platevision;

struct Options {
    fs::path manifest;
    fs::path output_json;
    fs::path detector_model;
    fs::path recognizer_model;
    fs::path classifier_model =
        fs::u8path(PLATEVISION_CONFIG_DIR).parent_path() /
        "third_party/hyperlpr/models/20230229/onnx/"
        "litemodel_cls_96x_r1.onnx";
    std::string engine{"onnx"};
    std::string mode{"end-to-end"};
    fs::path alphabet_path =
        fs::u8path(PLATEVISION_CONFIG_DIR) / "plate_alphabet.txt";
    fs::path hyperlpr_tokens_path =
        fs::u8path(PLATEVISION_CONFIG_DIR) / "hyperlpr_tokens.txt";
    std::string detector_layout{"channels-first"};
    std::string recognizer_layout{"time-major"};
    bool coordinates_normalized{false};
    float score_threshold{0.35f};
    float iou_threshold{0.45f};
    float match_iou_threshold{0.50f};
    int detector_input_width{0};
    int detector_input_height{0};
    int recognizer_input_width{0};
    int recognizer_input_height{0};
    int intra_threads{1};
    std::string provider{"cpu"};
    std::string detector_provider{"cpu"};
    std::string recognizer_provider{"cpu"};
    int device_id{0};
    bool show_help{false};
};

struct Summary {
    std::size_t samples{0};
    std::size_t loaded{0};
    std::size_t detection_found{0};
    std::size_t box_samples{0};
    std::size_t detection_matches{0};
    std::size_t valid_predictions{0};
    std::size_t exact_matches{0};
    double mean_character_accuracy{0.0};
    double p50_latency_ms{0.0};
    double p95_latency_ms{0.0};
};

std::string trim(std::string value) {
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), [&](char character) {
            return !is_space(static_cast<unsigned char>(character));
        }));
    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            [&](char character) {
                return !is_space(static_cast<unsigned char>(character));
            })
            .base(),
        value.end());
    return value;
}

std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (character == '"') {
            if (quoted && index + 1 < line.size() &&
                line[index + 1] == '"') {
                current.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == ',' && !quoted) {
            fields.push_back(trim(std::move(current)));
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    fields.push_back(trim(std::move(current)));
    return fields;
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream stream;
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (character < 0x20U) {
                stream << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0')
                       << static_cast<int>(character) << std::dec;
            } else {
                stream << static_cast<char>(character);
            }
            break;
        }
    }
    return stream.str();
}

bool parseFloat(const std::string& text, float& value) {
    try {
        std::size_t consumed = 0;
        const float parsed = std::stof(text, &consumed);
        if (consumed != text.size() || !std::isfinite(parsed)) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseInt(const std::string& text, int& value) {
    try {
        std::size_t consumed = 0;
        const long parsed = std::stol(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        value = static_cast<int>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseOptions(
    const std::vector<std::string>& arguments,
    Options& options,
    std::string& error) {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        auto requireValue = [&](std::string& value) {
            if (index + 1 >= arguments.size()) {
                error = "missing value for option: " + argument;
                return false;
            }
            value = arguments[++index];
            return true;
        };

        std::string value;
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
        } else if (argument == "--manifest") {
            if (!requireValue(value)) {
                return false;
            }
            options.manifest = fs::u8path(value);
        } else if (argument == "--output-json") {
            if (!requireValue(value)) {
                return false;
            }
            options.output_json = fs::u8path(value);
        } else if (argument == "--detector-model") {
            if (!requireValue(value)) {
                return false;
            }
            options.detector_model = fs::u8path(value);
        } else if (argument == "--recognizer-model") {
            if (!requireValue(value)) {
                return false;
            }
            options.recognizer_model = fs::u8path(value);
        } else if (argument == "--classifier-model") {
            if (!requireValue(value)) {
                return false;
            }
            options.classifier_model = fs::u8path(value);
        } else if (argument == "--engine") {
            if (!requireValue(value)) {
                return false;
            }
            if (value != "onnx" && value != "hyperlpr") {
                error = "engine must be onnx or hyperlpr";
                return false;
            }
            options.engine = value;
        } else if (argument == "--mode") {
            if (!requireValue(value)) {
                return false;
            }
            if (value != "end-to-end" && value != "recognition") {
                error = "mode must be end-to-end or recognition";
                return false;
            }
            options.mode = value;
        } else if (argument == "--alphabet") {
            if (!requireValue(value)) {
                return false;
            }
            options.alphabet_path = fs::u8path(value);
        } else if (argument == "--hyperlpr-tokens") {
            if (!requireValue(value)) {
                return false;
            }
            options.hyperlpr_tokens_path = fs::u8path(value);
        } else if (argument == "--detector-layout") {
            if (!requireValue(value)) {
                return false;
            }
            if (value != "channels-first" &&
                value != "detections-first") {
                error = "invalid detector layout";
                return false;
            }
            options.detector_layout = value;
        } else if (argument == "--recognizer-layout") {
            if (!requireValue(value)) {
                return false;
            }
            if (value != "time-major" &&
                value != "class-major") {
                error = "invalid recognizer layout";
                return false;
            }
            options.recognizer_layout = value;
        } else if (argument == "--coords-normalized") {
            options.coordinates_normalized = true;
        } else if (argument == "--score-threshold") {
            if (!requireValue(value) ||
                !parseFloat(value, options.score_threshold) ||
                options.score_threshold < 0.0f ||
                options.score_threshold > 1.0f) {
                error = "invalid score threshold";
                return false;
            }
        } else if (argument == "--iou-threshold") {
            if (!requireValue(value) ||
                !parseFloat(value, options.iou_threshold) ||
                options.iou_threshold < 0.0f ||
                options.iou_threshold > 1.0f) {
                error = "invalid IOU threshold";
                return false;
            }
        } else if (argument == "--match-iou-threshold") {
            if (!requireValue(value) ||
                !parseFloat(value, options.match_iou_threshold) ||
                options.match_iou_threshold < 0.0f ||
                options.match_iou_threshold > 1.0f) {
                error = "invalid match IOU threshold";
                return false;
            }
        } else if (argument == "--detector-input-width") {
            if (!requireValue(value) ||
                !parseInt(value, options.detector_input_width) ||
                options.detector_input_width < 0) {
                error = "invalid detector input width";
                return false;
            }
        } else if (argument == "--detector-input-height") {
            if (!requireValue(value) ||
                !parseInt(value, options.detector_input_height) ||
                options.detector_input_height < 0) {
                error = "invalid detector input height";
                return false;
            }
        } else if (argument == "--recognizer-input-width") {
            if (!requireValue(value) ||
                !parseInt(value, options.recognizer_input_width) ||
                options.recognizer_input_width < 0) {
                error = "invalid recognizer input width";
                return false;
            }
        } else if (argument == "--recognizer-input-height") {
            if (!requireValue(value) ||
                !parseInt(value, options.recognizer_input_height) ||
                options.recognizer_input_height < 0) {
                error = "invalid recognizer input height";
                return false;
            }
        } else if (argument == "--intra-threads") {
            if (!requireValue(value) ||
                !parseInt(value, options.intra_threads) ||
                options.intra_threads <= 0) {
                error = "invalid intra-op thread count";
                return false;
            }
        } else if (argument == "--provider") {
            if (!requireValue(value)) {
                return false;
            }
            if (value != "cpu" && value != "dml") {
                error = "provider must be cpu or dml";
                return false;
            }
            options.provider = value;
            options.detector_provider = value;
            options.recognizer_provider = value;
        } else if (argument == "--detector-provider") {
            if (!requireValue(value)) {
                return false;
            }
            if (value != "cpu" && value != "dml") {
                error = "detector provider must be cpu or dml";
                return false;
            }
            options.detector_provider = value;
        } else if (argument == "--recognizer-provider") {
            if (!requireValue(value)) {
                return false;
            }
            if (value != "cpu" && value != "dml") {
                error = "recognizer provider must be cpu or dml";
                return false;
            }
            options.recognizer_provider = value;
        } else if (argument == "--device-id") {
            if (!requireValue(value) ||
                !parseInt(value, options.device_id) ||
                options.device_id < 0) {
                error = "invalid device id";
                return false;
            }
        } else {
            error = "unknown option: " + argument;
            return false;
        }
    }

    if (!options.show_help) {
        if (options.manifest.empty()) {
            error = "--manifest is required";
            return false;
        }
        if (options.recognizer_model.empty() ||
            (options.mode == "end-to-end" &&
             options.detector_model.empty())) {
            error =
                "recognizer model is required; detector is required "
                "for end-to-end mode";
            return false;
        }
    }
    return true;
}

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  plate_bench --manifest <labels.csv> "
        << "--detector-model <model> --recognizer-model <model> "
        << "[options]\n\n"
        << "Manifest columns:\n"
        << "  image,plate,color,x,y,width,height\n\n"
        << "Options:\n"
        << "  --output-json <path>\n"
        << "  --engine <onnx|hyperlpr>\n"
        << "  --mode <end-to-end|recognition>\n"
        << "  --alphabet <path>\n"
        << "  --hyperlpr-tokens <path>\n"
        << "  --classifier-model <path>\n"
        << "  --detector-layout <layout>\n"
        << "  --recognizer-layout <layout>\n"
        << "  --coords-normalized\n"
        << "  --score-threshold <v>\n"
        << "  --iou-threshold <v>\n"
        << "  --match-iou-threshold <v>\n"
        << "  --detector-input-width/height <n>\n"
        << "  --recognizer-input-width/height <n>\n"
        << "  --intra-threads <n>\n"
        << "  --provider <cpu|dml>\n"
        << "  --detector-provider <cpu|dml>\n"
        << "  --recognizer-provider <cpu|dml>\n"
        << "  --device-id <n>\n";
}

PlateColor parseColor(const std::string& value) {
    if (value == "blue") {
        return PlateColor::Blue;
    }
    if (value == "green") {
        return PlateColor::Green;
    }
    if (value == "yellow") {
        return PlateColor::Yellow;
    }
    return PlateColor::Unknown;
}

double percentile(std::vector<double> values, double ratio) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double position =
        ratio * static_cast<double>(values.size() - 1);
    const std::size_t low =
        static_cast<std::size_t>(std::floor(position));
    const std::size_t high =
        static_cast<std::size_t>(std::ceil(position));
    if (low == high) {
        return values[low];
    }
    const double weight = position - static_cast<double>(low);
    return values[low] * (1.0 - weight) + values[high] * weight;
}

Summary summarize(
    const std::vector<EvaluationResult>& results,
    float match_iou_threshold) {
    Summary summary;
    summary.samples = results.size();
    std::vector<double> latencies;
    double accuracy_sum = 0.0;

    for (const EvaluationResult& result : results) {
        if (result.image_loaded) {
            ++summary.loaded;
        }
        if (result.detection_found) {
            ++summary.detection_found;
        }
        if (result.sample.expected_box) {
            ++summary.box_samples;
            if (result.detection_iou &&
                *result.detection_iou >= match_iou_threshold) {
                ++summary.detection_matches;
            }
        }
        if (result.valid) {
            ++summary.valid_predictions;
        }
        if (result.exact_match) {
            ++summary.exact_matches;
        }
        accuracy_sum += result.character_accuracy;
        latencies.push_back(
            static_cast<double>(result.latency_us) / 1000.0);
    }

    if (!results.empty()) {
        summary.mean_character_accuracy =
            accuracy_sum / static_cast<double>(results.size());
    }
    summary.p50_latency_ms = percentile(latencies, 0.50);
    summary.p95_latency_ms = percentile(latencies, 0.95);
    return summary;
}

bool writeReport(
    const fs::path& path,
    const Options& options,
    const std::vector<EvaluationResult>& results,
    const Summary& summary,
    std::string& error) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "failed to open benchmark output: " + path.string();
        return false;
    }

    output << std::setprecision(8)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"manifest\": \""
           << jsonEscape(options.manifest.u8string()) << "\",\n"
           << "  \"mode\": \"" << jsonEscape(options.mode) << "\",\n"
           << "  \"summary\": {\n"
           << "    \"samples\": " << summary.samples << ",\n"
           << "    \"loaded\": " << summary.loaded << ",\n"
           << "    \"detection_found\": "
           << summary.detection_found << ",\n"
           << "    \"box_samples\": " << summary.box_samples << ",\n"
           << "    \"match_iou_threshold\": "
           << options.match_iou_threshold << ",\n"
           << "    \"detection_recall\": "
           << (summary.box_samples == 0
                   ? 0.0
                   : static_cast<double>(summary.detection_matches) /
                         static_cast<double>(summary.box_samples))
           << ",\n"
           << "    \"valid_predictions\": "
           << summary.valid_predictions << ",\n"
           << "    \"exact_matches\": "
           << summary.exact_matches << ",\n"
           << "    \"exact_match_rate\": "
           << (summary.samples == 0
                   ? 0.0
                   : static_cast<double>(summary.exact_matches) /
                         static_cast<double>(summary.samples))
           << ",\n"
           << "    \"mean_character_accuracy\": "
           << summary.mean_character_accuracy << ",\n"
           << "    \"p50_latency_ms\": "
           << summary.p50_latency_ms << ",\n"
           << "    \"p95_latency_ms\": "
           << summary.p95_latency_ms << "\n"
           << "  },\n"
           << "  \"samples\": [\n";

    for (std::size_t index = 0; index < results.size(); ++index) {
        const EvaluationResult& result = results[index];
        output << "    {"
               << "\"image\":\""
               << jsonEscape(result.sample.image_path.u8string())
               << "\","
               << "\"expected\":\""
               << jsonEscape(result.sample.expected_plate)
               << "\","
               << "\"predicted\":\""
               << jsonEscape(result.predicted_plate)
               << "\","
               << "\"predicted_color\":\""
               << toString(result.predicted_color)
               << "\","
               << "\"confidence\":" << result.confidence << ","
               << "\"valid\":" << (result.valid ? "true" : "false")
               << ","
               << "\"exact_match\":"
               << (result.exact_match ? "true" : "false") << ","
               << "\"character_accuracy\":"
               << result.character_accuracy << ","
               << "\"detection_found\":"
               << (result.detection_found ? "true" : "false")
               << ","
               << "\"image_loaded\":"
               << (result.image_loaded ? "true" : "false")
               << ","
               << "\"detection_iou\":";
        if (result.detection_iou) {
            output << *result.detection_iou;
        } else {
            output << "null";
        }
        output << ",\"latency_ms\":"
               << static_cast<double>(result.latency_us) / 1000.0
               << ",\"error\":\""
               << jsonEscape(result.error) << "\"}"
               << (index + 1 < results.size() ? "," : "")
               << "\n";
    }
    output << "  ]\n"
           << "}\n";
    error.clear();
    return true;
}

std::vector<EvaluationSample> loadManifest(
    const fs::path& manifest_path,
    std::string& error) {
    std::ifstream input(manifest_path, std::ios::binary);
    if (!input) {
        error = "failed to open manifest: " + manifest_path.string();
        return {};
    }

    std::string header_line;
    if (!std::getline(input, header_line)) {
        error = "manifest is empty";
        return {};
    }
    if (header_line.size() >= 3 &&
        static_cast<unsigned char>(header_line[0]) == 0xEFU &&
        static_cast<unsigned char>(header_line[1]) == 0xBBU &&
        static_cast<unsigned char>(header_line[2]) == 0xBFU) {
        header_line.erase(0, 3);
    }

    const std::vector<std::string> headers =
        parseCsvLine(header_line);
    std::map<std::string, std::size_t> columns;
    for (std::size_t index = 0; index < headers.size(); ++index) {
        columns[headers[index]] = index;
    }
    if (!columns.count("image") || !columns.count("plate")) {
        error = "manifest requires image and plate columns";
        return {};
    }

    const fs::path base_directory = manifest_path.parent_path();
    std::vector<EvaluationSample> samples;
    std::string line;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (trim(line).empty()) {
            continue;
        }
        const std::vector<std::string> fields = parseCsvLine(line);
        if (fields.size() < headers.size()) {
            error = "manifest row " + std::to_string(line_number) +
                " has too few columns";
            return {};
        }

        auto field = [&](const std::string& name) -> std::string {
            const auto iterator = columns.find(name);
            return iterator == columns.end()
                ? std::string{}
                : fields[iterator->second];
        };

        EvaluationSample sample;
        const std::string image = field("image");
        const fs::path image_path = fs::u8path(image);
        sample.image_path = image_path.is_absolute()
            ? image_path
            : base_directory / image_path;
        sample.expected_plate = field("plate");
        sample.expected_color = parseColor(field("color"));

        const std::string x = field("x");
        const std::string y = field("y");
        const std::string width = field("width");
        const std::string height = field("height");
        const bool has_box = !x.empty() || !y.empty() ||
            !width.empty() || !height.empty();
        if (has_box) {
            float parsed_x = 0.0f;
            float parsed_y = 0.0f;
            float parsed_width = 0.0f;
            float parsed_height = 0.0f;
            if (!parseFloat(x, parsed_x) ||
                !parseFloat(y, parsed_y) ||
                !parseFloat(width, parsed_width) ||
                !parseFloat(height, parsed_height) ||
                parsed_width <= 0.0f ||
                parsed_height <= 0.0f) {
                error = "manifest row " +
                    std::to_string(line_number) +
                    " has an invalid bounding box";
                return {};
            }
            sample.expected_box = cv::Rect2f(
                parsed_x,
                parsed_y,
                parsed_width,
                parsed_height);
        }
        samples.push_back(std::move(sample));
    }

    if (samples.empty()) {
        error = "manifest has no samples";
        return {};
    }
    error.clear();
    return samples;
}

int run(const std::vector<std::string>& arguments) {
    Options options;
    std::string error;
    if (!parseOptions(arguments, options, error)) {
        std::cerr << error << "\n\n";
        printUsage();
        return 1;
    }
    if (options.show_help) {
        printUsage();
        return 0;
    }

    const std::vector<EvaluationSample> samples =
        loadManifest(options.manifest, error);
    if (samples.empty()) {
        std::cerr << error << "\n";
        return 2;
    }

    std::unique_ptr<IPlateDetector> detector;
    std::unique_ptr<IPlateRecognizer> recognizer;
    const bool end_to_end = options.mode == "end-to-end";
    if (options.engine == "onnx") {
        SymbolTable symbols;
        if (!SymbolTable::loadFromFile(
                options.alphabet_path,
                symbols,
                error)) {
            std::cerr << error << "\n";
            return 3;
        }

        if (end_to_end) {
            OnnxDetectorConfig detector_config;
            detector_config.model_path = options.detector_model;
            detector_config.output_layout =
                options.detector_layout == "detections-first"
                    ? DetectionOutputLayout::DetectionsFirst
                    : DetectionOutputLayout::ChannelsFirst;
            detector_config.input_width = options.detector_input_width;
            detector_config.input_height = options.detector_input_height;
            detector_config.intra_threads = options.intra_threads;
            detector_config.provider =
                options.detector_provider == "dml"
                    ? ExecutionProvider::DirectML
                    : ExecutionProvider::Cpu;
            detector_config.device_id = options.device_id;
            detector_config.score_threshold = options.score_threshold;
            detector_config.iou_threshold = options.iou_threshold;
            detector_config.coordinates_normalized =
                options.coordinates_normalized;
            detector = createOnnxPlateDetector(detector_config, error);
            if (!detector) {
                std::cerr << error << "\n";
                return 3;
            }
        }

        OnnxRecognizerConfig recognizer_config;
        recognizer_config.model_path = options.recognizer_model;
        recognizer_config.symbols = symbols;
        recognizer_config.output_layout =
            options.recognizer_layout == "class-major"
                ? RecognitionOutputLayout::ClassMajor
                : RecognitionOutputLayout::TimeMajor;
        recognizer_config.input_width =
            options.recognizer_input_width;
        recognizer_config.input_height =
            options.recognizer_input_height;
        recognizer_config.intra_threads = options.intra_threads;
        recognizer_config.provider =
            options.recognizer_provider == "dml"
                ? ExecutionProvider::DirectML
                : ExecutionProvider::Cpu;
        recognizer_config.device_id = options.device_id;
        recognizer = createOnnxPlateRecognizer(
            recognizer_config,
            error);
        if (!recognizer) {
            std::cerr << error << "\n";
            return 3;
        }
    } else {
        SymbolTable symbols;
        if (!SymbolTable::loadFromFile(
                options.hyperlpr_tokens_path,
                symbols,
                error)) {
            std::cerr << error << "\n";
            return 3;
        }

        if (end_to_end) {
            HyperLprDetectorConfig detector_config;
            detector_config.model_path = options.detector_model;
            detector_config.intra_threads = options.intra_threads;
            detector_config.provider =
                options.detector_provider == "dml"
                    ? ExecutionProvider::DirectML
                    : ExecutionProvider::Cpu;
            detector_config.device_id = options.device_id;
            detector_config.score_threshold = options.score_threshold;
            detector_config.iou_threshold = options.iou_threshold;
            detector = createHyperLprPlateDetector(
                detector_config,
                error);
            if (!detector) {
                std::cerr << error << "\n";
                return 3;
            }
        }

        HyperLprRecognizerConfig recognizer_config;
        recognizer_config.model_path = options.recognizer_model;
        recognizer_config.classifier_model_path =
            options.classifier_model;
        recognizer_config.symbols = symbols;
        recognizer_config.intra_threads = options.intra_threads;
        recognizer_config.provider =
            options.recognizer_provider == "dml"
                ? ExecutionProvider::DirectML
                : ExecutionProvider::Cpu;
        recognizer_config.device_id = options.device_id;
        recognizer = createHyperLprPlateRecognizer(
            recognizer_config,
            error);
        if (!recognizer) {
            std::cerr << error << "\n";
            return 3;
        }
    }

    PerspectiveRectifier rectifier;
    std::unique_ptr<PlateEvaluator> evaluator;
    std::unique_ptr<CropPlateEvaluator> crop_evaluator;
    if (options.mode == "recognition") {
        crop_evaluator =
            std::make_unique<CropPlateEvaluator>(*recognizer);
    } else {
        evaluator = std::make_unique<PlateEvaluator>(
            *detector,
            rectifier,
            *recognizer);
    }
    std::vector<EvaluationResult> results;
    results.reserve(samples.size());
    for (const EvaluationSample& sample : samples) {
        results.push_back(
            options.mode == "recognition"
                ? crop_evaluator->evaluate(sample)
                : evaluator->evaluate(sample));
    }

    const Summary summary = summarize(
        results,
        options.match_iou_threshold);
    const fs::path output_json = options.output_json.empty()
        ? fs::path(options.manifest).replace_extension(
              ".benchmark.json")
        : options.output_json;
    if (!writeReport(
            output_json,
            options,
            results,
            summary,
            error)) {
        std::cerr << error << "\n";
        return 4;
    }

    const double exact_match_rate = summary.samples == 0
        ? 0.0
        : static_cast<double>(summary.exact_matches) /
            static_cast<double>(summary.samples);
    const double detection_recall = summary.box_samples == 0
        ? 0.0
        : static_cast<double>(summary.detection_matches) /
            static_cast<double>(summary.box_samples);
    std::cout
        << "samples=" << summary.samples << "\n"
        << "detection_found=" << summary.detection_found << "\n";
    if (summary.box_samples > 0) {
        std::cout << "detection_recall="
                  << detection_recall << "\n";
    } else {
        std::cout << "detection_recall=n/a\n";
    }
    std::cout
        << "exact_match_rate=" << exact_match_rate << "\n"
        << "mean_character_accuracy="
        << summary.mean_character_accuracy << "\n"
        << "p50_latency_ms=" << summary.p50_latency_ms << "\n"
        << "p95_latency_ms=" << summary.p95_latency_ms << "\n"
        << "json_report=" << output_json.string() << "\n";
    return 0;
}

#ifdef _WIN32
std::vector<std::string> narrowArguments(
    int argc,
    wchar_t** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(std::max(0, argc - 1)));
    for (int index = 1; index < argc; ++index) {
        const int required = WideCharToMultiByte(
            CP_UTF8,
            0,
            argv[index],
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);
        std::string value(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            argv[index],
            -1,
            value.data(),
            required,
            nullptr,
            nullptr);
        if (!value.empty() && value.back() == '\0') {
            value.pop_back();
        }
        arguments.push_back(std::move(value));
    }
    return arguments;
}
#endif

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    return run(narrowArguments(argc, argv));
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return run(arguments);
}
#endif

