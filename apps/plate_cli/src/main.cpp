#include "platevision/bounded_queue.hpp"
#include "platevision/capture_service.hpp"
#include "platevision/event_coordinator.hpp"
#include "platevision/mock_adapters.hpp"
#include "platevision/recognition_pipeline.hpp"
#include "platevision/simple_iou_tracker.hpp"
#include "platevision/storage/record_service.hpp"
#include "platevision/storage/snapshot_writer.hpp"
#include "platevision/storage/sqlite_recognition_repository.hpp"
#include "platevision/video/opencv_sources.hpp"
#include "platevision/video/sample_generator.hpp"

#ifdef PLATEVISION_HAS_ONNX
#include "platevision/inference/onnx_adapters.hpp"
#include "platevision/inference/symbol_table.hpp"
#endif

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace platevision;

enum class Engine {
    Mock,
    Onnx,
    HyperLpr
};

struct Options {
    fs::path input;
    fs::path output_video;
    fs::path output_json;
    Engine engine{Engine::Mock};
    std::string plate = u8"京A12345";
    PlateColor plate_color{PlateColor::Blue};
    fs::path detector_model;
    fs::path recognizer_model;
    fs::path classifier_model =
        fs::u8path(PLATEVISION_CONFIG_DIR).parent_path() /
        "third_party/hyperlpr/models/20230229/onnx/"
        "litemodel_cls_96x_r1.onnx";
    fs::path alphabet_path =
        fs::u8path(PLATEVISION_CONFIG_DIR) / "plate_alphabet.txt";
    fs::path hyperlpr_tokens_path =
        fs::u8path(PLATEVISION_CONFIG_DIR) / "hyperlpr_tokens.txt";
    std::string detector_layout{"channels-first"};
    std::string recognizer_layout{"time-major"};
    bool coordinates_normalized{false};
    float score_threshold{0.35f};
    float iou_threshold{0.45f};
    int detector_input_width{0};
    int detector_input_height{0};
    int recognizer_input_width{0};
    int recognizer_input_height{0};
    int intra_threads{1};
    std::string provider{"cpu"};
    std::string detector_provider{"cpu"};
    std::string recognizer_provider{"cpu"};
    int device_id{0};
    std::size_t queue_capacity{8};
    int max_frames{0};
    int fps{25};
    bool show_help{false};
    bool generate_sample{false};
    fs::path sample_path;
    int sample_frames{60};
    int sample_width{1920};
    int sample_height{1080};
    bool persistence_enabled{true};
    fs::path database_path{"data/plate_vision.db"};
    fs::path snapshot_directory{"data/snapshots"};
    int stable_hits{6};
    int vote_window{10};
    int record_cooldown_ms{30000};
    int min_track_duration_ms{800};
    bool ui_stream{false};
    bool control_stdin{false};
};

struct ObservationRecord {
    std::uint64_t frame_sequence{0};
    std::uint64_t track_id{0};
    std::string text;
    std::string color;
    float confidence{0.0f};
    bool valid{false};
    bool confirmed{false};
    cv::Rect2f box;
};

struct FrameRecord {
    std::uint64_t sequence{0};
    std::int64_t timestamp_us{0};
    std::vector<ObservationRecord> observations;
    StageTimings timings;
};

struct Report {
    fs::path input;
    std::string engine{"mock"};
    std::string mock_plate;
    std::uint64_t captured_frames{0};
    std::uint64_t processed_frames{0};
    std::uint64_t dropped_frames{0};
    bool persistence_enabled{true};
    fs::path database_path;
    std::vector<FrameRecord> frames;
    std::vector<RecognitionEvent> events;
};

struct TimingSummary {
    double detect_p50_ms{0.0};
    double detect_p95_ms{0.0};
    double recognize_p50_ms{0.0};
    double recognize_p95_ms{0.0};
    double total_p50_ms{0.0};
    double total_p95_ms{0.0};
    double average_total_ms{0.0};
};

std::string toLowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
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
        case '\b':
            stream << "\\b";
            break;
        case '\f':
            stream << "\\f";
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

std::string base64Encode(const std::vector<unsigned char>& input) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < input.size(); index += 3) {
        const unsigned int first = input[index];
        const unsigned int second =
            index + 1 < input.size() ? input[index + 1] : 0;
        const unsigned int third =
            index + 2 < input.size() ? input[index + 2] : 0;
        const unsigned int value =
            (first << 16U) | (second << 8U) | third;
        output.push_back(kAlphabet[(value >> 18U) & 0x3FU]);
        output.push_back(kAlphabet[(value >> 12U) & 0x3FU]);
        output.push_back(
            index + 1 < input.size()
                ? kAlphabet[(value >> 6U) & 0x3FU]
                : '=');
        output.push_back(
            index + 2 < input.size()
                ? kAlphabet[value & 0x3FU]
                : '=');
    }
    return output;
}

std::string jpegBase64(const cv::Mat& image, int maximum_width) {
    if (image.empty()) {
        return {};
    }
    cv::Mat resized = image;
    if (maximum_width > 0 && image.cols > maximum_width) {
        const double scale =
            static_cast<double>(maximum_width) /
            static_cast<double>(image.cols);
        cv::resize(
            image,
            resized,
            cv::Size(),
            scale,
            scale,
            cv::INTER_AREA);
    }
    std::vector<unsigned char> encoded;
    if (!cv::imencode(".jpg", resized, encoded)) {
        return {};
    }
    return base64Encode(encoded);
}

double microsecondsToMilliseconds(std::chrono::microseconds value) {
    return static_cast<double>(value.count()) / 1000.0;
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

TimingSummary summarizeTimings(const Report& report) {
    std::vector<double> detect;
    std::vector<double> recognize;
    std::vector<double> total;
    double total_sum = 0.0;

    for (const FrameRecord& frame : report.frames) {
        detect.push_back(microsecondsToMilliseconds(frame.timings.detect));
        recognize.push_back(
            microsecondsToMilliseconds(frame.timings.recognize));
        const double total_ms =
            microsecondsToMilliseconds(frame.timings.total);
        total.push_back(total_ms);
        total_sum += total_ms;
    }

    TimingSummary summary;
    summary.detect_p50_ms = percentile(detect, 0.50);
    summary.detect_p95_ms = percentile(detect, 0.95);
    summary.recognize_p50_ms = percentile(recognize, 0.50);
    summary.recognize_p95_ms = percentile(recognize, 0.95);
    summary.total_p50_ms = percentile(total, 0.50);
    summary.total_p95_ms = percentile(total, 0.95);
    summary.average_total_ms =
        total.empty() ? 0.0 : total_sum / static_cast<double>(total.size());
    return summary;
}

void writeObservation(
    std::ostream& output,
    const ObservationRecord& record) {
    output << "{"
           << "\"frame_sequence\":" << record.frame_sequence << ","
           << "\"track_id\":" << record.track_id << ","
           << "\"text\":\"" << jsonEscape(record.text) << "\","
           << "\"color\":\"" << jsonEscape(record.color) << "\","
           << "\"confidence\":" << record.confidence << ","
           << "\"valid\":" << (record.valid ? "true" : "false") << ","
           << "\"confirmed\":"
           << (record.confirmed ? "true" : "false") << ","
           << "\"box\":{"
           << "\"x\":" << record.box.x << ","
           << "\"y\":" << record.box.y << ","
           << "\"width\":" << record.box.width << ","
           << "\"height\":" << record.box.height
           << "}}";
}

void writeEvent(
    std::ostream& output,
    const RecognitionEvent& event) {
    output << "{"
           << "\"event_id\":" << event.event_id << ","
           << "\"camera_id\":" << event.camera_id << ","
           << "\"stream_id\":" << event.stream_id << ","
           << "\"track_id\":" << event.track_id << ","
           << "\"plate_number\":\""
           << jsonEscape(event.plate_number) << "\","
           << "\"color\":\"" << toString(event.color) << "\","
           << "\"confidence\":" << event.confidence << ","
           << "\"captured_at_us\":" << event.captured_at_us << ","
           << "\"snapshot_path\":\""
           << jsonEscape(event.snapshot_path) << "\","
           << "\"box\":{"
           << "\"x\":" << event.bounding_box.x << ","
           << "\"y\":" << event.bounding_box.y << ","
           << "\"width\":" << event.bounding_box.width << ","
           << "\"height\":" << event.bounding_box.height
           << "}}";
}

bool writeJsonReport(
    const fs::path& output_path,
    const Report& report,
    std::string& error) {
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        error = "failed to open JSON output: " + output_path.string();
        return false;
    }

    const TimingSummary timing = summarizeTimings(report);
    output << std::setprecision(8);
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"engine\": \"" << jsonEscape(report.engine) << "\",\n"
           << "  \"input\": \""
           << jsonEscape(report.input.u8string()) << "\",\n"
           << "  \"mock_plate\": \"" << jsonEscape(report.mock_plate) << "\",\n"
           << "  \"captured_frames\": " << report.captured_frames << ",\n"
           << "  \"processed_frames\": " << report.processed_frames << ",\n"
           << "  \"dropped_frames\": " << report.dropped_frames << ",\n"
           << "  \"persistence_enabled\": "
           << (report.persistence_enabled ? "true" : "false") << ",\n"
           << "  \"database\": \""
           << jsonEscape(report.database_path.u8string()) << "\",\n"
           << "  \"timing_ms\": {\n"
           << "    \"detect_p50\": " << timing.detect_p50_ms << ",\n"
           << "    \"detect_p95\": " << timing.detect_p95_ms << ",\n"
           << "    \"recognize_p50\": " << timing.recognize_p50_ms << ",\n"
           << "    \"recognize_p95\": " << timing.recognize_p95_ms << ",\n"
           << "    \"total_p50\": " << timing.total_p50_ms << ",\n"
           << "    \"total_p95\": " << timing.total_p95_ms << ",\n"
           << "    \"total_average\": " << timing.average_total_ms << "\n"
           << "  },\n"
           << "  \"frames\": [\n";

    for (std::size_t frame_index = 0;
         frame_index < report.frames.size();
         ++frame_index) {
        const FrameRecord& frame = report.frames[frame_index];
        output << "    {\n"
               << "      \"sequence\": " << frame.sequence << ",\n"
               << "      \"timestamp_us\": " << frame.timestamp_us << ",\n"
               << "      \"observations\": [";

        for (std::size_t observation_index = 0;
             observation_index < frame.observations.size();
             ++observation_index) {
            if (observation_index > 0) {
                output << ",";
            }
            writeObservation(
                output,
                frame.observations[observation_index]);
        }

        output << "]\n"
               << "    }";
        if (frame_index + 1 < report.frames.size()) {
            output << ",";
        }
        output << "\n";
    }

    output << "  ],\n"
           << "  \"recognized_events\": [\n";
    for (std::size_t event_index = 0;
         event_index < report.events.size();
         ++event_index) {
        output << "    ";
        writeEvent(output, report.events[event_index]);
        if (event_index + 1 < report.events.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ]\n"
           << "}\n";

    if (!output) {
        error = "failed while writing JSON output: " + output_path.string();
        return false;
    }
    return true;
}

fs::path defaultVideoOutput(const fs::path& input) {
    fs::path result = input;
    result.replace_extension(".annotated.mp4");
    return result;
}

fs::path defaultJsonOutput(const fs::path& input) {
    fs::path result = input;
    result.replace_extension(".results.json");
    return result;
}

bool parseSizeT(const std::string& text, std::size_t& value) {
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        value = static_cast<std::size_t>(parsed);
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
        } else if (argument == "--input") {
            if (!requireValue(value)) {
                return false;
            }
            options.input = fs::u8path(value);
        } else if (argument == "--output-video") {
            if (!requireValue(value)) {
                return false;
            }
            options.output_video = fs::u8path(value);
        } else if (argument == "--output-json") {
            if (!requireValue(value)) {
                return false;
            }
            options.output_json = fs::u8path(value);
        } else if (argument == "--engine") {
            if (!requireValue(value)) {
                return false;
            }
            const std::string engine = toLowerAscii(value);
            if (engine == "mock") {
                options.engine = Engine::Mock;
            } else if (engine == "onnx") {
                options.engine = Engine::Onnx;
            } else if (engine == "hyperlpr") {
                options.engine = Engine::HyperLpr;
            } else {
                error = "engine must be mock, onnx, or hyperlpr";
                return false;
            }
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
            const std::string layout = toLowerAscii(value);
            if (layout != "channels-first" &&
                layout != "detections-first") {
                error =
                    "detector layout must be channels-first or "
                    "detections-first";
                return false;
            }
            options.detector_layout = layout;
        } else if (argument == "--recognizer-layout") {
            if (!requireValue(value)) {
                return false;
            }
            const std::string layout = toLowerAscii(value);
            if (layout != "time-major" &&
                layout != "class-major") {
                error =
                    "recognizer layout must be time-major or class-major";
                return false;
            }
            options.recognizer_layout = layout;
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
            const std::string provider = toLowerAscii(value);
            if (provider != "cpu" && provider != "dml") {
                error = "provider must be cpu or dml";
                return false;
            }
            options.provider = provider;
            options.detector_provider = provider;
            options.recognizer_provider = provider;
        } else if (argument == "--detector-provider") {
            if (!requireValue(value)) {
                return false;
            }
            const std::string provider = toLowerAscii(value);
            if (provider != "cpu" && provider != "dml") {
                error = "detector provider must be cpu or dml";
                return false;
            }
            options.detector_provider = provider;
        } else if (argument == "--recognizer-provider") {
            if (!requireValue(value)) {
                return false;
            }
            const std::string provider = toLowerAscii(value);
            if (provider != "cpu" && provider != "dml") {
                error = "recognizer provider must be cpu or dml";
                return false;
            }
            options.recognizer_provider = provider;
        } else if (argument == "--device-id") {
            if (!requireValue(value) ||
                !parseInt(value, options.device_id) ||
                options.device_id < 0) {
                error = "invalid device id";
                return false;
            }
        } else if (argument == "--no-records") {
            options.persistence_enabled = false;
        } else if (argument == "--database") {
            if (!requireValue(value)) {
                return false;
            }
            options.database_path = fs::u8path(value);
        } else if (argument == "--snapshot-dir") {
            if (!requireValue(value)) {
                return false;
            }
            options.snapshot_directory = fs::u8path(value);
        } else if (argument == "--stable-hits") {
            if (!requireValue(value) ||
                !parseInt(value, options.stable_hits) ||
                options.stable_hits <= 0) {
                error = "invalid stable hits";
                return false;
            }
        } else if (argument == "--vote-window") {
            if (!requireValue(value) ||
                !parseInt(value, options.vote_window) ||
                options.vote_window <= 0) {
                error = "invalid vote window";
                return false;
            }
        } else if (argument == "--record-cooldown-ms") {
            if (!requireValue(value) ||
                !parseInt(value, options.record_cooldown_ms) ||
                options.record_cooldown_ms < 0) {
                error = "invalid record cooldown";
                return false;
            }
        } else if (argument == "--min-track-duration-ms") {
            if (!requireValue(value) ||
                !parseInt(value, options.min_track_duration_ms) ||
                options.min_track_duration_ms < 0) {
                error = "invalid minimum track duration";
                return false;
            }
        } else if (argument == "--ui-stream") {
            options.ui_stream = true;
        } else if (argument == "--control-stdin") {
            options.control_stdin = true;
        } else if (argument == "--plate") {
            if (!requireValue(value)) {
                return false;
            }
            options.plate = value;
        } else if (argument == "--color") {
            if (!requireValue(value)) {
                return false;
            }
            const std::string color = toLowerAscii(value);
            if (color == "blue") {
                options.plate_color = PlateColor::Blue;
            } else if (color == "green") {
                options.plate_color = PlateColor::Green;
            } else {
                error = "plate color must be blue or green";
                return false;
            }
        } else if (argument == "--queue-capacity") {
            if (!requireValue(value) ||
                !parseSizeT(value, options.queue_capacity) ||
                options.queue_capacity == 0) {
                error = "invalid queue capacity";
                return false;
            }
        } else if (argument == "--max-frames") {
            if (!requireValue(value) ||
                !parseInt(value, options.max_frames) ||
                options.max_frames < 0) {
                error = "invalid max frame count";
                return false;
            }
        } else if (argument == "--fps") {
            if (!requireValue(value) ||
                !parseInt(value, options.fps) ||
                options.fps <= 0) {
                error = "invalid output FPS";
                return false;
            }
        } else if (argument == "--generate-sample") {
            options.generate_sample = true;
            if (!requireValue(value)) {
                return false;
            }
            options.sample_path = fs::u8path(value);
        } else if (argument == "--frames") {
            if (!requireValue(value) ||
                !parseInt(value, options.sample_frames) ||
                options.sample_frames <= 0) {
                error = "invalid sample frame count";
                return false;
            }
        } else if (argument == "--width") {
            if (!requireValue(value) ||
                !parseInt(value, options.sample_width) ||
                options.sample_width <= 0) {
                error = "invalid sample width";
                return false;
            }
        } else if (argument == "--height") {
            if (!requireValue(value) ||
                !parseInt(value, options.sample_height) ||
                options.sample_height <= 0) {
                error = "invalid sample height";
                return false;
            }
        } else {
            error = "unknown option: " + argument;
            return false;
        }
    }

    if (!options.generate_sample && !options.show_help &&
        options.input.empty()) {
        error = "--input is required";
        return false;
    }

    if (options.engine != Engine::Mock && !options.show_help) {
#ifdef PLATEVISION_HAS_ONNX
        if (options.detector_model.empty() ||
            options.recognizer_model.empty()) {
            error =
                "non-mock engines require detector and recognizer models";
            return false;
        }
#else
        error = "this build does not include ONNX Runtime";
        return false;
#endif
    }
    return true;
}

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  plate_cli --input <image-or-video> [options]\n"
        << "  plate_cli --generate-sample <video> [options]\n\n"
        << "Options:\n"
        << "  --output-video <path>   annotated output video\n"
        << "  --output-json <path>    JSON report path\n"
        << "  --engine <mock|onnx|hyperlpr> inference engine\n"
        << "  --detector-model <path> ONNX plate detector model\n"
        << "  --recognizer-model <path> ONNX plate recognizer model\n"
        << "  --classifier-model <path> HyperLPR color classifier\n"
        << "  --alphabet <path>       CTC symbol table\n"
        << "  --hyperlpr-tokens <path> HyperLPR token table\n"
        << "  --detector-layout <layout> channels-first|detections-first\n"
        << "  --recognizer-layout <layout> time-major|class-major\n"
        << "  --coords-normalized     detector boxes are normalized\n"
        << "  --score-threshold <v>   detector score threshold\n"
        << "  --iou-threshold <v>     detector NMS IOU threshold\n"
        << "  --detector-input-width <n>\n"
        << "  --detector-input-height <n>\n"
        << "  --recognizer-input-width <n>\n"
        << "  --recognizer-input-height <n>\n"
        << "  --intra-threads <n>     ONNX Runtime intra-op threads\n"
        << "  --provider <cpu|dml>    execution provider\n"
        << "  --detector-provider <cpu|dml>\n"
        << "  --recognizer-provider <cpu|dml>\n"
        << "  --device-id <n>         DirectML adapter index\n"
        << "  --no-records            disable SQLite and snapshot output\n"
        << "  --database <path>       SQLite database path\n"
        << "  --snapshot-dir <path>   snapshot directory\n"
        << "  --stable-hits <n>       votes required for a stable plate\n"
        << "  --vote-window <n>       observations kept for voting\n"
        << "  --record-cooldown-ms <n> duplicate record cooldown\n"
        << "  --min-track-duration-ms <n> stable observation time\n"
        << "  --ui-stream             emit JSON Lines for the desktop UI\n"
        << "  --control-stdin         stop gracefully on stdin 'stop'\n"
        << "  --plate <text>          mock recognized plate\n"
        << "  --color <blue|green>    mock plate color\n"
        << "  --queue-capacity <n>    bounded frame queue capacity\n"
        << "  --max-frames <n>        stop after n processed frames\n"
        << "  --fps <n>               output video FPS\n"
        << "  --frames <n>            synthetic sample frame count\n"
        << "  --width <n>             synthetic sample width\n"
        << "  --height <n>            synthetic sample height\n";
}

void drawResult(cv::Mat& image, const FrameResult& result) {
    for (const PlateObservation& observation : result.observations) {
        const cv::Rect box(
            static_cast<int>(observation.box.bounds.x),
            static_cast<int>(observation.box.bounds.y),
            static_cast<int>(observation.box.bounds.width),
            static_cast<int>(observation.box.bounds.height));
        const cv::Scalar color = observation.valid
            ? cv::Scalar(30, 210, 80)
            : cv::Scalar(20, 20, 220);
        cv::rectangle(image, box, color, 4);

        std::string ascii_text;
        for (const unsigned char character : observation.text) {
            if (character < 0x80U &&
                std::isprint(character) != 0) {
                ascii_text.push_back(static_cast<char>(character));
            }
        }
        if (ascii_text.empty()) {
            ascii_text = "PLATE";
        }

        const std::string label =
            ascii_text + (observation.valid ? " [valid]" : " [invalid]");
        int baseline = 0;
        const cv::Size text_size = cv::getTextSize(
            label,
            cv::FONT_HERSHEY_SIMPLEX,
            0.9,
            2,
            &baseline);
        const int label_width = std::min(
            image.cols,
            text_size.width + 16);
        const int label_x = std::clamp(
            box.x,
            0,
            std::max(0, image.cols - label_width));
        const int label_y = std::max(
            0,
            box.y - text_size.height - 18);
        cv::rectangle(
            image,
            cv::Rect(
                label_x,
                label_y,
                label_width,
                text_size.height + 16),
            color,
            cv::FILLED);
        cv::putText(
            image,
            label,
            cv::Point(
                label_x + 8,
                std::max(
                    text_size.height + 4,
                    label_y + text_size.height + 4)),
            cv::FONT_HERSHEY_SIMPLEX,
            0.9,
            cv::Scalar(15, 15, 15),
            2,
            cv::LINE_AA);
    }

    if (result.frame) {
        const std::string frame_label =
            "frame " + std::to_string(result.frame->sequence);
        cv::putText(
            image,
            frame_label,
            cv::Point(32, image.rows - 32),
            cv::FONT_HERSHEY_SIMPLEX,
            0.9,
            cv::Scalar(230, 235, 240),
            2,
            cv::LINE_AA);
    }
}

cv::Mat makeSnapshot(
    const Frame& frame,
    const cv::Rect2f& bounding_box) {
    if (frame.bgr.empty()) {
        return {};
    }
    const float padding_x = bounding_box.width * 0.20f;
    const float padding_y = bounding_box.height * 0.30f;
    cv::Rect2f expanded(
        bounding_box.x - padding_x,
        bounding_box.y - padding_y,
        bounding_box.width + padding_x * 2.0f,
        bounding_box.height + padding_y * 2.0f);
    expanded &= cv::Rect2f(
        0.0f,
        0.0f,
        static_cast<float>(frame.bgr.cols),
        static_cast<float>(frame.bgr.rows));
    if (expanded.width <= 1.0f || expanded.height <= 1.0f) {
        return {};
    }
    return frame.bgr(
        cv::Rect(
            static_cast<int>(expanded.x),
            static_cast<int>(expanded.y),
            static_cast<int>(expanded.width),
            static_cast<int>(expanded.height)))
        .clone();
}

int runPipeline(const Options& options) {
    const std::int64_t record_started_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    std::string error;
    std::unique_ptr<IVideoSource> source =
        createOpenCvMediaSource(options.input, error);
    if (!source) {
        std::cerr << error << "\n";
        return 2;
    }

    std::shared_ptr<PlaybackController> playback;
    if (options.ui_stream) {
        playback = std::make_shared<PlaybackController>(
            static_cast<double>(options.fps));
    }
    BoundedLatestQueue<FrameRef> queue(options.queue_capacity);
    CaptureService capture(*source, queue, 0, playback);
    if (!capture.start(error)) {
        std::cerr << error << "\n";
        return 3;
    }

    std::unique_ptr<IPlateDetector> detector;
    std::unique_ptr<IPlateRecognizer> recognizer;
    if (options.engine == Engine::Mock) {
        detector = std::make_unique<MockPlateDetector>();
        recognizer = std::make_unique<MockPlateRecognizer>(
            options.plate,
            options.plate_color,
            0.99f);
    } else if (options.engine == Engine::Onnx) {
#ifdef PLATEVISION_HAS_ONNX
        SymbolTable symbols;
        if (!SymbolTable::loadFromFile(
                options.alphabet_path,
                symbols,
                error)) {
            std::cerr << error << "\n";
            return 3;
        }

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
#else
        std::cerr << "this build does not include ONNX Runtime\n";
        return 3;
#endif
    } else {
#ifdef PLATEVISION_HAS_ONNX
        SymbolTable symbols;
        if (!SymbolTable::loadFromFile(
                options.hyperlpr_tokens_path,
                symbols,
                error)) {
            std::cerr << error << "\n";
            return 3;
        }

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
#else
        std::cerr << "this build does not include ONNX Runtime\n";
        return 3;
#endif
    }

    PerspectiveRectifier rectifier;
    SimpleIouTracker tracker;
    RecognitionPipeline pipeline(
        *detector,
        rectifier,
        *recognizer,
        tracker);

    EventCoordinatorConfig event_config;
    event_config.camera_id = 1;
    event_config.voter.stable_hits = options.stable_hits;
    event_config.voter.window_size =
        static_cast<std::size_t>(options.vote_window);
    event_config.duplicate_cooldown =
        std::chrono::milliseconds(options.record_cooldown_ms);
    event_config.min_track_duration =
        std::chrono::milliseconds(options.min_track_duration_ms);
    EventCoordinator event_coordinator(event_config);

    std::unique_ptr<RecordService> record_service;
    if (options.persistence_enabled) {
        auto repository =
            std::make_unique<SqliteRecognitionRepository>(
                options.database_path);
        auto snapshot_writer =
            std::make_unique<SnapshotWriter>(
                options.snapshot_directory);
        record_service = std::make_unique<RecordService>(
            std::move(repository),
            std::move(snapshot_writer),
            [](const std::string& record_error) {
                std::cerr << "record error: " << record_error << "\n";
            });
        if (!record_service->start(error)) {
            std::cerr << error << "\n";
            return 3;
        }
    }

    const fs::path output_video = options.output_video.empty()
        ? defaultVideoOutput(options.input)
        : options.output_video;
    const fs::path output_json = options.output_json.empty()
        ? defaultJsonOutput(options.input)
        : options.output_json;

    if (output_video.has_parent_path()) {
        std::filesystem::create_directories(output_video.parent_path());
    }

    cv::VideoWriter writer;
    bool writer_ready = false;
    Report report;
    report.input = options.input;
    report.persistence_enabled = options.persistence_enabled;
    report.database_path = options.database_path;
    report.engine =
        options.engine == Engine::Mock
            ? "mock"
            : (options.engine == Engine::Onnx ? "onnx" : "hyperlpr");
    report.mock_plate =
        options.engine == Engine::Mock ? options.plate : "";
    if (options.ui_stream) {
        std::cout
            << "{\"type\":\"status\",\"message\":\"running\"}\n";
        std::cout.flush();
    }

    auto external_stop = std::make_shared<std::atomic<bool>>(false);
    if (options.control_stdin) {
        std::thread([external_stop, playback] {
            std::string command;
            while (std::getline(std::cin, command)) {
                if (command == "stop" || command == "q") {
                    external_stop->store(true);
                    return;
                }
                if (playback) {
                    if (command == "pause") {
                        playback->setPaused(true);
                    } else if (command == "resume") {
                        playback->setPaused(false);
                    } else if (command.rfind("speed ", 0) == 0) {
                        try {
                            playback->setSpeed(
                                std::stof(command.substr(6)));
                        } catch (...) {
                            // Ignore malformed playback commands.
                        }
                    }
                }
            }
        }).detach();
    }
    bool stop_requested = false;
    while (!stop_requested && !external_stop->load()) {
        FrameRef frame;
        if (!queue.pop(frame, std::chrono::milliseconds(250))) {
            if (queue.closed() && queue.empty()) {
                break;
            }
            continue;
        }

        FrameResult result = pipeline.process(std::move(frame));
        if (!result.error.empty()) {
            std::cerr << "pipeline error: " << result.error << "\n";
            if (options.ui_stream) {
                std::cout
                    << "{\"type\":\"error\",\"message\":\""
                    << jsonEscape(result.error) << "\"}\n";
                std::cout.flush();
            }
            break;
        }

        FrameRecord frame_record;
        frame_record.sequence = result.frame->sequence;
        frame_record.timestamp_us = result.frame->source_timestamp_us;
        frame_record.timings = result.timings;

        for (std::size_t index = 0;
             index < result.observations.size();
             ++index) {
            const PlateObservation& observation = result.observations[index];
            const PlateTrack& track = result.tracks[index];
            ObservationRecord record;
            record.frame_sequence = frame_record.sequence;
            record.track_id = track.track_id;
            record.text = observation.text;
            record.color = toString(observation.color);
            record.confidence = observation.text_confidence;
            record.valid = observation.valid;
            record.confirmed = track.confirmed;
            record.box = observation.box.bounds;
            frame_record.observations.push_back(record);
        }

        report.frames.push_back(frame_record);
        ++report.processed_frames;

        const std::vector<RecognitionEvent> events =
            event_coordinator.process(result);
        std::vector<std::string> event_snapshots;
        event_snapshots.reserve(events.size());
        for (const RecognitionEvent& event : events) {
            report.events.push_back(event);
            const cv::Mat snapshot =
                makeSnapshot(*result.frame, event.bounding_box);
            event_snapshots.push_back(
                options.ui_stream
                    ? jpegBase64(snapshot, 480)
                    : std::string{});
            if (record_service) {
                RecordTask task;
                task.event = event;
                task.snapshot = snapshot;
                if (!record_service->enqueue(std::move(task))) {
                    std::cerr
                        << "failed to enqueue recognition record\n";
                }
            }
        }

        if (options.ui_stream && result.frame &&
            !result.frame->bgr.empty()) {
            std::cout
                << "{\"type\":\"frame\","
                << "\"sequence\":" << result.frame->sequence << ","
                << "\"frame_width\":" << result.frame->bgr.cols << ","
                << "\"frame_height\":" << result.frame->bgr.rows << ","
                << "\"image_jpeg\":\""
                << jpegBase64(result.frame->bgr, 1280) << "\","
                << "\"observations\":[";
            for (std::size_t index = 0;
                 index < frame_record.observations.size();
                 ++index) {
                if (index > 0) {
                    std::cout << ",";
                }
                writeObservation(
                    std::cout,
                    frame_record.observations[index]);
            }
            std::cout << "],\"events\":[";
            for (std::size_t index = 0;
                 index < events.size();
                 ++index) {
                if (index > 0) {
                    std::cout << ",";
                }
                const RecognitionEvent& event = events[index];
                std::cout
                    << "{"
                    << "\"plate_number\":\""
                    << jsonEscape(event.plate_number) << "\","
                    << "\"color\":\"" << toString(event.color) << "\","
                    << "\"confidence\":" << event.confidence << ","
                    << "\"captured_at_us\":"
                    << event.captured_at_us << ","
                    << "\"snapshot_jpeg\":\""
                    << event_snapshots[index] << "\","
                    << "\"box\":{"
                    << "\"x\":" << event.bounding_box.x << ","
                    << "\"y\":" << event.bounding_box.y << ","
                    << "\"width\":" << event.bounding_box.width << ","
                    << "\"height\":" << event.bounding_box.height
                    << "}}";
            }
            std::cout << "]}\n";
            std::cout.flush();

        }

        if (!options.ui_stream && result.frame &&
            !result.frame->bgr.empty()) {
            if (!writer_ready) {
                const int fourcc =
                    cv::VideoWriter::fourcc('m', 'p', '4', 'v');
                writer_ready = writer.open(
                    output_video.string(),
                    fourcc,
                    static_cast<double>(options.fps),
                    result.frame->bgr.size());
                if (!writer_ready) {
                    std::cerr
                        << "failed to open annotated video: "
                        << output_video.string() << "\n";
                    break;
                }
            }

            cv::Mat annotated = result.frame->bgr.clone();
            drawResult(annotated, result);
            writer.write(annotated);
        }

        if (options.max_frames > 0 &&
            report.processed_frames >=
                static_cast<std::uint64_t>(options.max_frames)) {
            stop_requested = true;
        }
    }

    capture.stop();
    source->close();
    queue.close();
    if (record_service) {
        record_service->flushAndStop();
        RecordQuery query;
        query.begin_ms = record_started_ms;
        query.limit = 10000;
        std::string query_error;
        const std::vector<RecognitionEvent> stored_events =
            record_service->query(query, query_error);
        if (query_error.empty()) {
            report.events = stored_events;
        } else {
            std::cerr << "record query error: "
                      << query_error << "\n";
        }
    }
    report.dropped_frames = queue.droppedCount();
    report.captured_frames =
        report.processed_frames + report.dropped_frames;
    if (writer_ready) {
        writer.release();
    }

    if (options.ui_stream) {
        std::cout
            << "{\"type\":\"finished\","
            << "\"processed_frames\":" << report.processed_frames << ","
            << "\"dropped_frames\":" << report.dropped_frames << ","
            << "\"recognized_events\":" << report.events.size()
            << "}\n";
        std::cout.flush();
        return report.processed_frames > 0 ? 0 : 5;
    }

    if (!writeJsonReport(output_json, report, error)) {
        std::cerr << error << "\n";
        return 4;
    }

    const TimingSummary timing = summarizeTimings(report);
    std::uint64_t valid_observations = 0;
    std::uint64_t confirmed_observations = 0;
    for (const FrameRecord& frame : report.frames) {
        for (const ObservationRecord& observation : frame.observations) {
            if (observation.valid) {
                ++valid_observations;
            }
            if (observation.valid && observation.confirmed) {
                ++confirmed_observations;
            }
        }
    }

    std::cout
        << "processed_frames=" << report.processed_frames << "\n"
        << "dropped_frames=" << report.dropped_frames << "\n"
        << "valid_observations=" << valid_observations << "\n"
        << "confirmed_observations=" << confirmed_observations << "\n"
        << "recognized_events=" << report.events.size() << "\n"
        << "total_p50_ms=" << timing.total_p50_ms << "\n"
        << "total_p95_ms=" << timing.total_p95_ms << "\n"
        << "annotated_video=" << output_video.string() << "\n"
        << "json_report=" << output_json.string() << "\n";
    if (report.persistence_enabled) {
        std::cout << "database=" << options.database_path.string()
                  << "\n"
                  << "snapshot_dir="
                  << options.snapshot_directory.string() << "\n";
    }

    return report.processed_frames > 0 ? 0 : 5;
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

int runMain(const std::vector<std::string>& arguments) {
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

    if (options.generate_sample) {
        if (!generateSampleVideo(
                options.sample_path,
                options.sample_width,
                options.sample_height,
                options.sample_frames,
                static_cast<double>(options.fps),
                error)) {
            std::cerr << error << "\n";
            return 2;
        }
        std::cout
            << "sample_video=" << options.sample_path.string() << "\n"
            << "frames=" << options.sample_frames << "\n"
            << "size=" << options.sample_width << "x"
            << options.sample_height << "\n";
        return 0;
    }

    return runPipeline(options);
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    return runMain(narrowArguments(argc, argv));
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(std::max(0, argc - 1)));
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return runMain(arguments);
}
#endif

