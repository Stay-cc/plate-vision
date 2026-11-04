#include "platevision/inference/onnx_adapters.hpp"

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

#ifdef PLATEVISION_HAS_DIRECTML
#include <dml_provider_factory.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace platevision {
namespace {

Ort::Env& ortEnvironment() {
    static Ort::Env environment(
        ORT_LOGGING_LEVEL_ERROR,
        "PlateVision");
    return environment;
}

std::size_t tensorElementCount(
    const std::vector<std::int64_t>& shape) {
    std::size_t count = 1;
    for (const std::int64_t dimension : shape) {
        if (dimension <= 0) {
            return 0;
        }
        count *= static_cast<std::size_t>(dimension);
    }
    return count;
}

class OrtModel final {
public:
    bool initialize(
        const std::filesystem::path& model_path,
        int intra_threads,
        ExecutionProvider provider,
        int device_id,
        std::string& error) {
        try {
            if (!std::filesystem::exists(model_path)) {
                error = "model file does not exist: " +
                    model_path.string();
                return false;
            }

            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(std::max(1, intra_threads));
            options.SetGraphOptimizationLevel(
                GraphOptimizationLevel::ORT_ENABLE_ALL);
            if (provider == ExecutionProvider::DirectML) {
#ifdef PLATEVISION_HAS_DIRECTML
                Ort::Status status(
                    OrtSessionOptionsAppendExecutionProvider_DML(
                        options,
                        device_id));
                if (!status.IsOK()) {
                    error = "failed to enable DirectML provider: " +
                        status.GetErrorMessage();
                    return false;
                }
#else
                error = "DirectML provider is not compiled in";
                return false;
#endif
            }

#ifdef _WIN32
            const std::wstring native_path = model_path.wstring();
            session_ = std::make_unique<Ort::Session>(
                ortEnvironment(),
                native_path.c_str(),
                options);
#else
            const std::string native_path = model_path.string();
            session_ = std::make_unique<Ort::Session>(
                ortEnvironment(),
                native_path.c_str(),
                options);
#endif

            if (session_->GetInputCount() != 1 ||
                session_->GetOutputCount() < 1) {
                error = "model must have one input and at least one output";
                return false;
            }

            Ort::AllocatorWithDefaultOptions allocator;
            auto input_name =
                session_->GetInputNameAllocated(0, allocator);
            auto output_name =
                session_->GetOutputNameAllocated(0, allocator);
            input_name_ = input_name.get();
            output_name_ = output_name.get();

            const auto input_type =
                session_->GetInputTypeInfo(0)
                    .GetTensorTypeAndShapeInfo();
            if (input_type.GetElementType() !=
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                error = "model input must be float32";
                return false;
            }
            input_shape_ = input_type.GetShape();
        } catch (const Ort::Exception& exception) {
            error = std::string("failed to load ONNX model: ") +
                exception.what();
            return false;
        } catch (const std::exception& exception) {
            error = std::string("failed to load ONNX model: ") +
                exception.what();
            return false;
        }

        error.clear();
        return true;
    }

    const std::vector<std::int64_t>& inputShape() const noexcept {
        return input_shape_;
    }

    bool run(
        const std::vector<float>& input,
        const std::vector<std::int64_t>& input_shape,
        std::vector<float>& output,
        std::vector<std::int64_t>& output_shape,
        std::string& error) {
        try {
            Ort::MemoryInfo memory_info =
                Ort::MemoryInfo::CreateCpu(
                    OrtArenaAllocator,
                    OrtMemTypeDefault);
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                memory_info,
                const_cast<float*>(input.data()),
                input.size(),
                input_shape.data(),
                input_shape.size());

            const char* input_names[] = {input_name_.c_str()};
            const char* output_names[] = {output_name_.c_str()};
            auto outputs = session_->Run(
                Ort::RunOptions{nullptr},
                input_names,
                &input_tensor,
                1,
                output_names,
                1);

            const auto info = outputs[0].GetTensorTypeAndShapeInfo();
            output_shape = info.GetShape();
            const std::size_t count = tensorElementCount(output_shape);
            if (count == 0) {
                error = "model output has an unsupported dynamic shape";
                return false;
            }

            const float* data = outputs[0].GetTensorData<float>();
            output.assign(data, data + count);
        } catch (const Ort::Exception& exception) {
            error = std::string("ONNX inference failed: ") +
                exception.what();
            return false;
        } catch (const std::exception& exception) {
            error = std::string("ONNX inference failed: ") +
                exception.what();
            return false;
        }

        error.clear();
        return true;
    }

private:
    std::unique_ptr<Ort::Session> session_;
    std::string input_name_;
    std::string output_name_;
    std::vector<std::int64_t> input_shape_;
};

struct LetterboxResult {
    cv::Mat image;
    float scale{1.0f};
    int pad_x{0};
    int pad_y{0};
};

LetterboxResult letterbox(
    const cv::Mat& image,
    int target_width,
    int target_height,
    cv::Scalar padding = cv::Scalar(114, 114, 114)) {
    LetterboxResult result;
    if (image.empty() || target_width <= 0 || target_height <= 0) {
        return result;
    }

    result.scale = std::min(
        static_cast<float>(target_width) /
            static_cast<float>(image.cols),
        static_cast<float>(target_height) /
            static_cast<float>(image.rows));
    const int resized_width = std::max(
        1,
        static_cast<int>(
            std::round(static_cast<float>(image.cols) * result.scale)));
    const int resized_height = std::max(
        1,
        static_cast<int>(
            std::round(static_cast<float>(image.rows) * result.scale)));

    cv::Mat resized;
    cv::resize(
        image,
        resized,
        cv::Size(resized_width, resized_height));

    result.pad_x = (target_width - resized_width) / 2;
    result.pad_y = (target_height - resized_height) / 2;
    cv::copyMakeBorder(
        resized,
        result.image,
        result.pad_y,
        target_height - resized_height - result.pad_y,
        result.pad_x,
        target_width - resized_width - result.pad_x,
        cv::BORDER_CONSTANT,
        padding);
    return result;
}

std::vector<float> toNchwTensor(
    const cv::Mat& image,
    bool normalize) {
    if (image.empty() || image.type() != CV_8UC3) {
        return {};
    }

    const int width = image.cols;
    const int height = image.rows;
    const std::size_t plane_size =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);
    std::vector<float> tensor(plane_size * 3);

    for (int y = 0; y < height; ++y) {
        const cv::Vec3b* row = image.ptr<cv::Vec3b>(y);
        for (int x = 0; x < width; ++x) {
            const std::size_t offset =
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            const float factor = normalize ? 1.0f / 255.0f : 1.0f;
            tensor[offset] = static_cast<float>(row[x][2]) * factor;
            tensor[plane_size + offset] =
                static_cast<float>(row[x][1]) * factor;
            tensor[plane_size * 2 + offset] =
                static_cast<float>(row[x][0]) * factor;
        }
    }
    return tensor;
}

struct Detection {
    cv::Rect2f bounds;
    float score{0.0f};
    std::array<cv::Point2f, 4> quad{};
    bool has_quad{false};
};

float intersectionOverUnion(
    const cv::Rect2f& left,
    const cv::Rect2f& right) {
    const float intersection_width = std::max(
        0.0f,
        std::min(left.x + left.width, right.x + right.width) -
            std::max(left.x, right.x));
    const float intersection_height = std::max(
        0.0f,
        std::min(left.y + left.height, right.y + right.height) -
            std::max(left.y, right.y));
    const float intersection = intersection_width * intersection_height;
    const float union_area =
        left.width * left.height + right.width * right.height -
        intersection;
    return union_area <= 0.0f ? 0.0f : intersection / union_area;
}

std::vector<Detection> nonMaximumSuppression(
    std::vector<Detection> detections,
    float iou_threshold,
    std::size_t max_detections) {
    std::sort(
        detections.begin(),
        detections.end(),
        [](const Detection& left, const Detection& right) {
            return left.score > right.score;
        });

    std::vector<Detection> selected;
    selected.reserve(std::min(max_detections, detections.size()));
    for (const Detection& candidate : detections) {
        bool suppressed = false;
        for (const Detection& kept : selected) {
            if (intersectionOverUnion(candidate.bounds, kept.bounds) >
                iou_threshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            selected.push_back(candidate);
            if (selected.size() >= max_detections) {
                break;
            }
        }
    }
    return selected;
}

bool resolveInputSize(
    const std::vector<std::int64_t>& shape,
    int configured_width,
    int configured_height,
    int& width,
    int& height,
    std::string& error) {
    width = configured_width;
    height = configured_height;
    if (shape.size() == 4) {
        if (width <= 0 && shape[3] > 0) {
            width = static_cast<int>(shape[3]);
        }
        if (height <= 0 && shape[2] > 0) {
            height = static_cast<int>(shape[2]);
        }
    }

    if (width <= 0 || height <= 0) {
        error =
            "model input width and height must be static or configured";
        return false;
    }
    return true;
}

class OnnxPlateDetector final : public IPlateDetector {
public:
    explicit OnnxPlateDetector(OnnxDetectorConfig config)
        : config_(std::move(config)) {}

    bool initialize(std::string& error) {
        if (!model_.initialize(
                config_.model_path,
                config_.intra_threads,
                config_.provider,
                config_.device_id,
                error)) {
            return false;
        }
        return resolveInputSize(
            model_.inputShape(),
            config_.input_width,
            config_.input_height,
            input_width_,
            input_height_,
            error);
    }

    std::vector<PlateBox> detect(
        const Frame& frame,
        std::string& error) override {
        error.clear();
        if (frame.bgr.empty()) {
            error = "input frame is empty";
            return {};
        }

        const LetterboxResult letterboxed =
            letterbox(frame.bgr, input_width_, input_height_);
        if (letterboxed.image.empty()) {
            error = "failed to preprocess detector input";
            return {};
        }

        const std::vector<float> input =
            toNchwTensor(letterboxed.image, true);
        const std::vector<std::int64_t> input_shape = {
            1,
            3,
            static_cast<std::int64_t>(input_height_),
            static_cast<std::int64_t>(input_width_)};

        std::vector<float> output;
        std::vector<std::int64_t> output_shape;
        if (!model_.run(input, input_shape, output, output_shape, error)) {
            return {};
        }

        if (output_shape.size() < 2) {
            error = "detector output shape is unsupported";
            return {};
        }

        std::size_t count = 0;
        std::size_t channels = 0;
        bool channels_first =
            config_.output_layout ==
            DetectionOutputLayout::ChannelsFirst;
        if (output_shape.size() == 3) {
            if (channels_first) {
                channels = static_cast<std::size_t>(output_shape[1]);
                count = static_cast<std::size_t>(output_shape[2]);
            } else {
                count = static_cast<std::size_t>(output_shape[1]);
                channels = static_cast<std::size_t>(output_shape[2]);
            }
        } else {
            count = static_cast<std::size_t>(output_shape[0]);
            channels = static_cast<std::size_t>(output_shape[1]);
            channels_first = false;
        }

        if (channels < 5 || count == 0 ||
            output.size() < count * channels) {
            error = "detector output tensor is too small";
            return {};
        }

        auto valueAt = [&](std::size_t index, std::size_t channel) {
            return channels_first
                ? output[channel * count + index]
                : output[index * channels + channel];
        };

        std::vector<Detection> candidates;
        candidates.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            float score = 0.0f;
            for (std::size_t channel = 4; channel < channels; ++channel) {
                score = std::max(score, valueAt(index, channel));
            }
            if (score < config_.score_threshold) {
                continue;
            }

            float center_x = valueAt(index, 0);
            float center_y = valueAt(index, 1);
            float box_width = valueAt(index, 2);
            float box_height = valueAt(index, 3);
            if (config_.coordinates_normalized) {
                center_x *= static_cast<float>(input_width_);
                center_y *= static_cast<float>(input_height_);
                box_width *= static_cast<float>(input_width_);
                box_height *= static_cast<float>(input_height_);
            }

            const float x =
                (center_x - box_width * 0.5f -
                 static_cast<float>(letterboxed.pad_x)) /
                letterboxed.scale;
            const float y =
                (center_y - box_height * 0.5f -
                 static_cast<float>(letterboxed.pad_y)) /
                letterboxed.scale;
            const float width = box_width / letterboxed.scale;
            const float height = box_height / letterboxed.scale;

            const cv::Rect2f frame_bounds(
                0.0f,
                0.0f,
                static_cast<float>(frame.bgr.cols),
                static_cast<float>(frame.bgr.rows));
            cv::Rect2f box(x, y, width, height);
            box &= frame_bounds;
            if (box.width <= 1.0f || box.height <= 1.0f) {
                continue;
            }
            candidates.push_back({box, score});
        }

        const std::vector<Detection> selected = nonMaximumSuppression(
            std::move(candidates),
            config_.iou_threshold,
            config_.max_detections);

        std::vector<PlateBox> results;
        results.reserve(selected.size());
        for (const Detection& detection : selected) {
            PlateBox box;
            box.bounds = detection.bounds;
            box.quad = {
                cv::Point2f(detection.bounds.x, detection.bounds.y),
                cv::Point2f(
                    detection.bounds.x + detection.bounds.width,
                    detection.bounds.y),
                cv::Point2f(
                    detection.bounds.x + detection.bounds.width,
                    detection.bounds.y + detection.bounds.height),
                cv::Point2f(
                    detection.bounds.x,
                    detection.bounds.y + detection.bounds.height)};
            box.confidence = detection.score;
            results.push_back(box);
        }
        return results;
    }

private:
    OnnxDetectorConfig config_;
    OrtModel model_;
    int input_width_{0};
    int input_height_{0};
};

class OnnxPlateRecognizer final : public IPlateRecognizer {
public:
    explicit OnnxPlateRecognizer(OnnxRecognizerConfig config)
        : config_(std::move(config)) {}

    bool initialize(std::string& error) {
        if (!model_.initialize(
                config_.model_path,
                config_.intra_threads,
                config_.provider,
                config_.device_id,
                error)) {
            return false;
        }
        if (config_.symbols.empty()) {
            error = "symbol table is empty";
            return false;
        }
        return resolveInputSize(
            model_.inputShape(),
            config_.input_width,
            config_.input_height,
            input_width_,
            input_height_,
            error);
    }

    PlateText recognize(
        const cv::Mat& plate_image,
        std::string& error) override {
        error.clear();
        if (plate_image.empty()) {
            error = "plate image is empty";
            return {};
        }

        cv::Mat resized;
        cv::resize(
            plate_image,
            resized,
            cv::Size(input_width_, input_height_),
            0.0,
            0.0,
            cv::INTER_LINEAR);
        const std::vector<float> input =
            toNchwTensor(resized, true);
        const std::vector<std::int64_t> input_shape = {
            1,
            3,
            static_cast<std::int64_t>(input_height_),
            static_cast<std::int64_t>(input_width_)};

        std::vector<float> output;
        std::vector<std::int64_t> output_shape;
        if (!model_.run(input, input_shape, output, output_shape, error)) {
            return {};
        }

        if (output_shape.size() != 2 && output_shape.size() != 3) {
            error = "recognizer output shape is unsupported";
            return {};
        }

        std::size_t steps = 0;
        std::size_t classes = 0;
        std::size_t base = 0;
        if (output_shape.size() == 3) {
            base = static_cast<std::size_t>(output_shape[0]);
            if (config_.output_layout ==
                RecognitionOutputLayout::TimeMajor) {
                steps = static_cast<std::size_t>(output_shape[1]);
                classes = static_cast<std::size_t>(output_shape[2]);
            } else {
                classes = static_cast<std::size_t>(output_shape[1]);
                steps = static_cast<std::size_t>(output_shape[2]);
            }
        } else if (
            config_.output_layout ==
            RecognitionOutputLayout::TimeMajor) {
            steps = static_cast<std::size_t>(output_shape[0]);
            classes = static_cast<std::size_t>(output_shape[1]);
        } else {
            classes = static_cast<std::size_t>(output_shape[0]);
            steps = static_cast<std::size_t>(output_shape[1]);
        }

        if (base > 1 || steps == 0 || classes == 0 ||
            output.size() < base * steps * classes) {
            error = "recognizer output tensor is unsupported";
            return {};
        }
        if (classes != config_.symbols.size()) {
            error =
                "recognizer class count does not match symbol table";
            return {};
        }

        const bool time_major =
            config_.output_layout ==
            RecognitionOutputLayout::TimeMajor;
        auto logitAt = [&](std::size_t step, std::size_t token) {
            return time_major
                ? output[step * classes + token]
                : output[token * steps + step];
        };

        std::string text;
        double confidence_sum = 0.0;
        std::size_t emitted = 0;
        int previous = -1;
        for (std::size_t step = 0; step < steps; ++step) {
            float maximum = -std::numeric_limits<float>::infinity();
            for (std::size_t token = 0; token < classes; ++token) {
                maximum = std::max(maximum, logitAt(step, token));
            }

            double denominator = 0.0;
            for (std::size_t token = 0; token < classes; ++token) {
                denominator += std::exp(
                    static_cast<double>(logitAt(step, token) - maximum));
            }

            int best = 0;
            float best_value = -std::numeric_limits<float>::infinity();
            for (std::size_t token = 0; token < classes; ++token) {
                const float value = logitAt(step, token);
                if (value > best_value) {
                    best_value = value;
                    best = static_cast<int>(token);
                }
            }

            if (best != config_.blank_index && best != previous) {
                text += config_.symbols.token(
                    static_cast<std::size_t>(best));
                const double probability =
                    denominator > 0.0
                        ? std::exp(
                              static_cast<double>(best_value - maximum)) /
                            denominator
                        : 0.0;
                confidence_sum += probability;
                ++emitted;
            }
            previous = best;
        }

        PlateText result;
        result.text = std::move(text);
        result.confidence = emitted > 0
            ? static_cast<float>(confidence_sum /
                                 static_cast<double>(emitted))
            : 0.0f;
        result.color =
            emitted == 8
                ? PlateColor::Green
                : (emitted == 7
                       ? PlateColor::Blue
                       : PlateColor::Unknown);
        return result;
    }

private:
    OnnxRecognizerConfig config_;
    OrtModel model_;
    int input_width_{0};
    int input_height_{0};
};

class HyperLprPlateDetector final : public IPlateDetector {
public:
    explicit HyperLprPlateDetector(HyperLprDetectorConfig config)
        : config_(std::move(config)) {}

    bool initialize(std::string& error) {
        if (!model_.initialize(
                config_.model_path,
                config_.intra_threads,
                config_.provider,
                config_.device_id,
                error)) {
            return false;
        }
        return resolveInputSize(
            model_.inputShape(),
            0,
            0,
            input_width_,
            input_height_,
            error);
    }

    std::vector<PlateBox> detect(
        const Frame& frame,
        std::string& error) override {
        error.clear();
        if (frame.bgr.empty()) {
            error = "input frame is empty";
            return {};
        }
        if (input_width_ != input_height_ ||
            (input_width_ != 320 && input_width_ != 640)) {
            error = "HyperLPR detector input must be 320x320 or 640x640";
            return {};
        }

        const LetterboxResult letterboxed = letterbox(
            frame.bgr,
            input_width_,
            input_height_,
            cv::Scalar(0, 0, 0));
        if (letterboxed.image.empty()) {
            error = "failed to preprocess HyperLPR detector input";
            return {};
        }

        const std::vector<float> input =
            toNchwTensor(letterboxed.image, true);
        const std::vector<std::int64_t> input_shape = {
            1,
            3,
            static_cast<std::int64_t>(input_height_),
            static_cast<std::int64_t>(input_width_)};

        std::vector<float> output;
        std::vector<std::int64_t> output_shape;
        if (!model_.run(input, input_shape, output, output_shape, error)) {
            return {};
        }

        const std::size_t expected_rows =
            input_width_ == 640 ? 25200U : 6300U;
        if (output_shape.size() != 3 ||
            output_shape[1] !=
                static_cast<std::int64_t>(expected_rows) ||
            output_shape[2] != 15 ||
            output.size() < expected_rows * 15U) {
            error = "HyperLPR detector output shape is unsupported";
            return {};
        }

        std::vector<Detection> candidates;
        candidates.reserve(expected_rows);
        for (std::size_t row = 0; row < expected_rows; ++row) {
            const float* values = output.data() + row * 15U;
            const float objectness = values[4];
            if (objectness < config_.score_threshold) {
                continue;
            }

            const float class0 = values[13] * objectness;
            const float class1 = values[14] * objectness;
            const float score = std::max(class0, class1);
            if (score < config_.score_threshold) {
                continue;
            }

            const float center_x = values[0];
            const float center_y = values[1];
            const float box_width = values[2];
            const float box_height = values[3];
            if (box_width <= 1.0f || box_height <= 1.0f) {
                continue;
            }

            const float x =
                (center_x - box_width * 0.5f -
                 static_cast<float>(letterboxed.pad_x)) /
                letterboxed.scale;
            const float y =
                (center_y - box_height * 0.5f -
                 static_cast<float>(letterboxed.pad_y)) /
                letterboxed.scale;
            cv::Rect2f bounds(
                x,
                y,
                box_width / letterboxed.scale,
                box_height / letterboxed.scale);
            bounds &= cv::Rect2f(
                0.0f,
                0.0f,
                static_cast<float>(frame.bgr.cols),
                static_cast<float>(frame.bgr.rows));
            if (bounds.width <= 1.0f || bounds.height <= 1.0f) {
                continue;
            }

            std::array<cv::Point2f, 4> quad{};
            for (int point = 0; point < 4; ++point) {
                quad[static_cast<std::size_t>(point)] = cv::Point2f(
                    (values[5 + point * 2] -
                     static_cast<float>(letterboxed.pad_x)) /
                        letterboxed.scale,
                    (values[6 + point * 2] -
                     static_cast<float>(letterboxed.pad_y)) /
                        letterboxed.scale);
            }
            candidates.push_back({bounds, score, quad, true});
        }

        const std::vector<Detection> selected = nonMaximumSuppression(
            std::move(candidates),
            config_.iou_threshold,
            config_.max_detections);

        std::vector<PlateBox> results;
        results.reserve(selected.size());
        for (const Detection& detection : selected) {
            PlateBox box;
            box.bounds = detection.bounds;
            if (detection.has_quad) {
                box.quad = detection.quad;
            } else {
                box.quad = {
                    cv::Point2f(
                        detection.bounds.x,
                        detection.bounds.y),
                    cv::Point2f(
                        detection.bounds.x + detection.bounds.width,
                        detection.bounds.y),
                    cv::Point2f(
                        detection.bounds.x + detection.bounds.width,
                        detection.bounds.y + detection.bounds.height),
                    cv::Point2f(
                        detection.bounds.x,
                        detection.bounds.y + detection.bounds.height)};
            }
            box.confidence = detection.score;
            results.push_back(box);
        }
        return results;
    }

private:
    HyperLprDetectorConfig config_;
    OrtModel model_;
    int input_width_{0};
    int input_height_{0};
};

class HyperLprPlateRecognizer final : public IPlateRecognizer {
public:
    explicit HyperLprPlateRecognizer(HyperLprRecognizerConfig config)
        : config_(std::move(config)) {}

    bool initialize(std::string& error) {
        if (!model_.initialize(
                config_.model_path,
                config_.intra_threads,
                config_.provider,
                config_.device_id,
                error)) {
            return false;
        }
        if (config_.symbols.empty()) {
            error = "HyperLPR symbol table is empty";
            return false;
        }
        if (!config_.classifier_model_path.empty()) {
            if (!classifier_model_.initialize(
                    config_.classifier_model_path,
                    config_.intra_threads,
                    config_.provider,
                    config_.device_id,
                    error)) {
                return false;
            }
            if (!resolveInputSize(
                    classifier_model_.inputShape(),
                    0,
                    0,
                    classifier_input_width_,
                    classifier_input_height_,
                    error)) {
                return false;
            }
            classifier_available_ = true;
        }
        return resolveInputSize(
            model_.inputShape(),
            0,
            0,
            input_width_,
            input_height_,
            error);
    }

    PlateText recognize(
        const cv::Mat& plate_image,
        std::string& error) override {
        error.clear();
        if (plate_image.empty()) {
            error = "plate image is empty";
            return {};
        }

        const float ratio =
            static_cast<float>(plate_image.cols) /
            static_cast<float>(plate_image.rows);
        const int resized_width = std::clamp(
            static_cast<int>(
                std::ceil(static_cast<float>(input_height_) * ratio)),
            48,
            input_width_);
        cv::Mat resized;
        cv::resize(
            plate_image,
            resized,
            cv::Size(resized_width, input_height_));

        const std::size_t plane_size =
            static_cast<std::size_t>(input_height_) *
            static_cast<std::size_t>(input_width_);
        std::vector<float> input(plane_size * 3U, 0.0f);
        for (int y = 0; y < input_height_; ++y) {
            const cv::Vec3b* row = resized.ptr<cv::Vec3b>(y);
            for (int x = 0; x < resized_width; ++x) {
                const std::size_t offset =
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(input_width_) +
                    static_cast<std::size_t>(x);
                input[offset] =
                    (static_cast<float>(row[x][0]) - 127.5f) / 127.5f;
                input[plane_size + offset] =
                    (static_cast<float>(row[x][1]) - 127.5f) / 127.5f;
                input[plane_size * 2U + offset] =
                    (static_cast<float>(row[x][2]) - 127.5f) / 127.5f;
            }
        }

        const std::vector<std::int64_t> input_shape = {
            1,
            3,
            static_cast<std::int64_t>(input_height_),
            static_cast<std::int64_t>(input_width_)};
        std::vector<float> output;
        std::vector<std::int64_t> output_shape;
        if (!model_.run(input, input_shape, output, output_shape, error)) {
            return {};
        }

        if (output_shape.size() != 3 ||
            output_shape[0] != 1 ||
            output_shape[1] <= 0 ||
            output_shape[2] <= 0) {
            error = "HyperLPR recognizer output shape is unsupported";
            return {};
        }
        const std::size_t steps =
            static_cast<std::size_t>(output_shape[1]);
        const std::size_t classes =
            static_cast<std::size_t>(output_shape[2]);
        if (classes != config_.symbols.size() ||
            output.size() < steps * classes) {
            error =
                "HyperLPR recognizer classes do not match symbol table";
            return {};
        }

        std::string text;
        double confidence_sum = 0.0;
        std::size_t emitted = 0;
        int previous = -1;
        for (std::size_t step = 0; step < steps; ++step) {
            const float* logits = output.data() + step * classes;
            float maximum = -std::numeric_limits<float>::infinity();
            for (std::size_t token = 0; token < classes; ++token) {
                maximum = std::max(maximum, logits[token]);
            }
            double denominator = 0.0;
            int best = 0;
            float best_value =
                -std::numeric_limits<float>::infinity();
            for (std::size_t token = 0; token < classes; ++token) {
                denominator += std::exp(
                    static_cast<double>(logits[token] - maximum));
                if (logits[token] > best_value) {
                    best_value = logits[token];
                    best = static_cast<int>(token);
                }
            }
            if (best != 0 && best != previous) {
                const std::string& symbol = config_.symbols.token(
                    static_cast<std::size_t>(best));
                if (symbol != "<unknown>") {
                    text += symbol;
                }
                const double probability =
                    denominator > 0.0
                        ? std::exp(
                              static_cast<double>(
                                  best_value - maximum)) /
                            denominator
                        : 0.0;
                confidence_sum += probability;
                ++emitted;
            }
            previous = best;
        }

        PlateText result;
        result.text = std::move(text);
        result.confidence = emitted > 0
            ? static_cast<float>(
                  confidence_sum / static_cast<double>(emitted))
            : 0.0f;
        std::string classifier_error;
        result.color = classifyColor(plate_image, classifier_error);
        if (result.color == PlateColor::Unknown) {
            result.color =
                emitted == 8
                    ? PlateColor::Green
                    : (emitted == 7
                           ? PlateColor::Blue
                           : PlateColor::Unknown);
        }
        return result;
    }

private:
    PlateColor classifyColor(
        const cv::Mat& plate_image,
        std::string& error) {
        if (!classifier_available_ || plate_image.empty()) {
            return PlateColor::Unknown;
        }

        cv::Mat resized;
        cv::resize(
            plate_image,
            resized,
            cv::Size(
                classifier_input_width_,
                classifier_input_height_));
        const std::size_t plane_size =
            static_cast<std::size_t>(classifier_input_width_) *
            static_cast<std::size_t>(classifier_input_height_);
        std::vector<float> input(plane_size * 3U);
        for (int y = 0; y < classifier_input_height_; ++y) {
            const cv::Vec3b* row = resized.ptr<cv::Vec3b>(y);
            for (int x = 0; x < classifier_input_width_; ++x) {
                const std::size_t offset =
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(
                            classifier_input_width_) +
                    static_cast<std::size_t>(x);
                input[offset] =
                    static_cast<float>(row[x][0]) / 255.0f;
                input[plane_size + offset] =
                    static_cast<float>(row[x][1]) / 255.0f;
                input[plane_size * 2U + offset] =
                    static_cast<float>(row[x][2]) / 255.0f;
            }
        }

        std::vector<float> output;
        std::vector<std::int64_t> output_shape;
        const std::vector<std::int64_t> input_shape = {
            1,
            3,
            static_cast<std::int64_t>(classifier_input_height_),
            static_cast<std::int64_t>(classifier_input_width_)};
        if (!classifier_model_.run(
                input,
                input_shape,
                output,
                output_shape,
                error) ||
            output.size() < 3) {
            return PlateColor::Unknown;
        }
        const auto maximum = std::max_element(
            output.begin(),
            output.begin() + 3);
        const std::size_t index = static_cast<std::size_t>(
            std::distance(output.begin(), maximum));
        if (index == 0) {
            return PlateColor::Blue;
        }
        if (index == 1) {
            return PlateColor::Green;
        }
        if (index == 2) {
            return PlateColor::Yellow;
        }
        return PlateColor::Unknown;
    }

    HyperLprRecognizerConfig config_;
    OrtModel model_;
    OrtModel classifier_model_;
    int input_width_{0};
    int input_height_{0};
    int classifier_input_width_{0};
    int classifier_input_height_{0};
    bool classifier_available_{false};
};

}  // namespace

std::unique_ptr<IPlateDetector> createOnnxPlateDetector(
    const OnnxDetectorConfig& config,
    std::string& error) {
    auto detector = std::make_unique<OnnxPlateDetector>(config);
    if (!detector->initialize(error)) {
        return nullptr;
    }
    return detector;
}

std::unique_ptr<IPlateRecognizer> createOnnxPlateRecognizer(
    const OnnxRecognizerConfig& config,
    std::string& error) {
    auto recognizer = std::make_unique<OnnxPlateRecognizer>(config);
    if (!recognizer->initialize(error)) {
        return nullptr;
    }
    return recognizer;
}

std::unique_ptr<IPlateDetector> createHyperLprPlateDetector(
    const HyperLprDetectorConfig& config,
    std::string& error) {
    auto detector = std::make_unique<HyperLprPlateDetector>(config);
    if (!detector->initialize(error)) {
        return nullptr;
    }
    return detector;
}

std::unique_ptr<IPlateRecognizer> createHyperLprPlateRecognizer(
    const HyperLprRecognizerConfig& config,
    std::string& error) {
    auto recognizer = std::make_unique<HyperLprPlateRecognizer>(config);
    if (!recognizer->initialize(error)) {
        return nullptr;
    }
    return recognizer;
}

}  // namespace platevision

