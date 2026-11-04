#include "platevision/bounded_queue.hpp"
#include "platevision/capture_service.hpp"
#include "platevision/evaluation/plate_evaluator.hpp"
#include "platevision/evaluation/plate_metrics.hpp"
#include "platevision/event_coordinator.hpp"
#include "platevision/mock_adapters.hpp"
#include "platevision/plate_validator.hpp"
#include "platevision/playback_controller.hpp"
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

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace platevision;

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << __FILE__ << ":" << __LINE__                         \
                      << " check failed: " #condition << "\n";               \
            return false;                                                    \
        }                                                                    \
    } while (false)

class FakeVideoSource final : public IVideoSource {
public:
    bool open(std::string& error) override {
        error.clear();
        opened_ = true;
        return true;
    }

    bool read(
        Frame& frame,
        std::chrono::milliseconds /*timeout*/) override {
        if (!opened_ || next_ >= 3) {
            return false;
        }
        frame.bgr = cv::Mat(12, 24, CV_8UC3, cv::Scalar(20, 30, 40));
        frame.source_timestamp_us =
            static_cast<std::int64_t>(next_) * 1000;
        ++next_;
        return true;
    }

    void close() override {
        opened_ = false;
    }

private:
    bool opened_{false};
    int next_{0};
};

bool testBoundedQueueDropsOldest() {
    BoundedLatestQueue<int> queue(2);
    CHECK(queue.pushLatest(1));
    CHECK(queue.pushLatest(2));
    CHECK(queue.pushLatest(3));

    int value = 0;
    CHECK(queue.pop(value, std::chrono::milliseconds(10)));
    CHECK(value == 2);
    CHECK(queue.pop(value, std::chrono::milliseconds(10)));
    CHECK(value == 3);
    CHECK(queue.droppedCount() == 1);
    return true;
}

bool testBoundedQueueClose() {
    BoundedLatestQueue<int> queue(1);
    queue.close();

    int value = 0;
    CHECK(queue.closed());
    CHECK(!queue.pop(value, std::chrono::milliseconds(5)));
    CHECK(!queue.pushLatest(7));
    return true;
}

bool testPlaybackController() {
    PlaybackController playback(25.0);
    CHECK(std::abs(playback.speed() - 1.0) < 1e-9);
    playback.setSpeed(2.0);
    CHECK(std::abs(playback.speed() - 2.0) < 1e-9);
    CHECK(!playback.paused());
    playback.setPaused(true);
    CHECK(playback.paused());
    playback.setPaused(false);
    CHECK(!playback.paused());
    return true;
}

bool testPlateValidator() {
    PlateValidator validator;
    std::string reason;

    PlateText blue;
    blue.text = u8"京A12345";
    blue.color = PlateColor::Blue;
    CHECK(validator.validate(blue, reason));

    PlateText green;
    green.text = u8"粤B12345D";
    green.color = PlateColor::Green;
    CHECK(validator.validate(green, reason));

    PlateText invalid_prefix;
    invalid_prefix.text = u8"AB12345";
    invalid_prefix.color = PlateColor::Blue;
    CHECK(!validator.validate(invalid_prefix, reason));

    PlateText wrong_length;
    wrong_length.text = u8"京A1234";
    wrong_length.color = PlateColor::Blue;
    CHECK(!validator.validate(wrong_length, reason));
    return true;
}

bool testPlateMetrics() {
    CHECK(PlateMetrics::utf8Length(u8"京A12345") == 7);
    CHECK(PlateMetrics::editDistance(u8"京A12345", u8"京A12346") == 1);
    CHECK(
        std::abs(
            PlateMetrics::characterAccuracy(
                u8"京A12345",
                u8"京A12345") -
            1.0) < 1e-9);
    CHECK(
        std::abs(
            PlateMetrics::characterAccuracy(
                u8"京A12345",
                u8"京A12346") -
            6.0 / 7.0) < 1e-9);
    CHECK(
        std::abs(
            PlateMetrics::intersectionOverUnion(
                0.0f, 0.0f, 10.0f, 10.0f,
                0.0f, 0.0f, 10.0f, 10.0f) -
            1.0f) < 1e-6f);
    CHECK(
        std::abs(
            PlateMetrics::intersectionOverUnion(
                0.0f, 0.0f, 10.0f, 10.0f,
                10.0f, 10.0f, 10.0f, 10.0f)) < 1e-6f);
    return true;
}

bool testStableTextVoter() {
    StableTextVoterConfig config;
    config.window_size = 5;
    config.stable_hits = 3;
    StableTextVoter voter(config);

    VoteResult result;
    for (int index = 0; index < 3; ++index) {
        result = voter.update(
            1,
            u8"京A12345",
            PlateColor::Blue,
            0.9f,
            1000 + index * 100);
    }
    CHECK(result.stable);
    CHECK(result.text == u8"京A12345");
    CHECK(result.color == PlateColor::Blue);
    CHECK(result.hit_count == 3);
    return true;
}

bool testDuplicateEventFilter() {
    DuplicateEventFilter filter(
        std::chrono::milliseconds(8000));
    CHECK(filter.shouldEmit(1, u8"京A12345", 0));
    CHECK(!filter.shouldEmit(1, u8"京A12345", 1000000));
    CHECK(filter.shouldEmit(1, u8"京A12345", 9000000));
    CHECK(filter.shouldEmit(2, u8"京A12345", 9500000));
    return true;
}

bool testEventCoordinator() {
    EventCoordinatorConfig config;
    config.camera_id = 1;
    config.voter.stable_hits = 6;
    config.voter.window_size = 10;
    EventCoordinator coordinator(config);

    Frame frame;
    frame.stream_id = 1;
    frame.bgr = cv::Mat(100, 200, CV_8UC3, cv::Scalar(10, 20, 30));

    std::vector<RecognitionEvent> events;
    for (int index = 0; index < 12; ++index) {
        frame.sequence = static_cast<std::uint64_t>(index);
        frame.source_timestamp_us =
            static_cast<std::int64_t>(index + 1) * 100000;

        FrameResult result;
        result.frame = std::make_shared<Frame>(frame);
        PlateObservation observation;
        observation.stream_id = 1;
        observation.frame_sequence = frame.sequence;
        observation.timestamp_us = frame.source_timestamp_us;
        observation.text = u8"京A12345";
        observation.color = PlateColor::Blue;
        observation.text_confidence = 0.95f;
        observation.valid = true;
        observation.box.bounds = cv::Rect2f(20, 20, 100, 30);
        PlateTrack track;
        track.track_id = 1;
        track.first_seen_us = 100000;
        track.confirmed = true;
        result.observations.push_back(observation);
        result.tracks.push_back(track);

        const std::vector<RecognitionEvent> frame_events =
            coordinator.process(result);
        events.insert(
            events.end(),
            frame_events.begin(),
            frame_events.end());
    }

    CHECK(events.size() == 1);
    CHECK(events[0].plate_number == u8"京A12345");
    CHECK(events[0].color == PlateColor::Blue);
    CHECK(events[0].camera_id == 1);

    frame.sequence = 99;
    frame.source_timestamp_us = 40'000'000;
    FrameResult late_result;
    late_result.frame = std::make_shared<Frame>(frame);
    PlateObservation late_observation;
    late_observation.stream_id = 1;
    late_observation.frame_sequence = 99;
    late_observation.timestamp_us = frame.source_timestamp_us;
    late_observation.text = u8"京A12345";
    late_observation.color = PlateColor::Blue;
    late_observation.text_confidence = 0.95f;
    late_observation.valid = true;
    late_observation.box.bounds = cv::Rect2f(20, 20, 100, 30);
    PlateTrack late_track;
    late_track.track_id = 1;
    late_track.first_seen_us = 100000;
    late_track.confirmed = true;
    late_result.observations.push_back(late_observation);
    late_result.tracks.push_back(late_track);
    const std::vector<RecognitionEvent> late_events =
        coordinator.process(late_result);
    CHECK(late_events.empty());
    return true;
}

bool testRecordServicePersistence() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("platevision_m3_" + std::to_string(
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count()));
    const std::filesystem::path database_path =
        root / "records.db";
    const std::filesystem::path snapshot_directory =
        root / "snapshots";

    const std::int64_t now_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    {
        auto repository =
            std::make_unique<SqliteRecognitionRepository>(database_path);
        auto writer =
            std::make_unique<SnapshotWriter>(snapshot_directory);
        RecordService service(
            std::move(repository),
            std::move(writer));
        std::string error;
        CHECK(service.start(error));

        RecognitionEvent event;
        event.stream_id = 1;
        event.camera_id = 1;
        event.track_id = 7;
        event.plate_number = u8"京A12345";
        event.color = PlateColor::Blue;
        event.confidence = 0.95f;
        event.captured_at_us = now_us;
        event.bounding_box = cv::Rect2f(10, 10, 120, 40);
        RecordTask task;
        task.event = event;
        task.snapshot = cv::Mat(
            80,
            240,
            CV_8UC3,
            cv::Scalar(240, 240, 240));
        CHECK(service.enqueue(std::move(task)));
        service.flushAndStop();
    }

    {
        SqliteRecognitionRepository repository(database_path);
        std::string error;
        CHECK(repository.initialize(error));
        RecordQuery query;
        query.plate_number = u8"京A12345";
        const std::vector<RecognitionEvent> events =
            repository.query(query, error);
        CHECK(error.empty());
        CHECK(events.size() == 1);
        CHECK(events[0].plate_number == u8"京A12345");
        CHECK(!events[0].snapshot_path.empty());
        CHECK(std::filesystem::exists(
            snapshot_directory /
            std::filesystem::u8path(events[0].snapshot_path)));
    }

    std::error_code filesystem_error;
    std::filesystem::remove_all(root, filesystem_error);
    CHECK(!filesystem_error);
    return true;
}

bool testRecognitionPipeline() {
    MockPlateDetector detector;
    PerspectiveRectifier rectifier;
    MockPlateRecognizer recognizer(
        u8"京A12345",
        PlateColor::Blue,
        0.98f);
    SimpleIouTracker tracker;
    RecognitionPipeline pipeline(detector, rectifier, recognizer, tracker);

    cv::Mat image(1080, 1920, CV_8UC3, cv::Scalar(30, 50, 70));
    Frame frame;
    frame.bgr = image;
    frame.source_timestamp_us = 1000;

    FrameResult result;
    for (int index = 0; index < 3; ++index) {
        frame.sequence = static_cast<std::uint64_t>(index);
        frame.source_timestamp_us =
            static_cast<std::int64_t>(index + 1) * 1000;
        result = pipeline.process(
            std::make_shared<Frame>(frame));
    }

    CHECK(result.error.empty());
    CHECK(result.observations.size() == 1);
    CHECK(result.tracks.size() == 1);
    CHECK(result.observations[0].valid);
    CHECK(result.observations[0].text == u8"京A12345");
    CHECK(result.tracks[0].track_id == 1);
    CHECK(result.tracks[0].hit_count == 3);
    CHECK(result.tracks[0].confirmed);
    CHECK(result.timings.total.count() > 0);
    return true;
}

bool testCaptureService() {
    FakeVideoSource source;
    std::string error;
    CHECK(source.open(error));

    BoundedLatestQueue<FrameRef> queue(4);
    CaptureService capture(source, queue);
    CHECK(capture.start(error));

    int frame_count = 0;
    std::int64_t first_source_timestamp = -1;
    for (int attempt = 0; attempt < 100; ++attempt) {
        FrameRef frame;
        if (queue.pop(frame, std::chrono::milliseconds(20))) {
            CHECK(frame);
            CHECK(frame->bgr.cols == 24);
            if (frame_count == 0) {
                first_source_timestamp = frame->source_timestamp_us;
            }
            ++frame_count;
            continue;
        }
        if (queue.closed() && queue.empty()) {
            break;
        }
    }

    capture.stop();
    CHECK(frame_count == 3);
    CHECK(first_source_timestamp == 0);
    CHECK(queue.closed());
    return true;
}

bool testOpenCvVideoRoundTrip() {
    const std::filesystem::path video_path =
        std::filesystem::temp_directory_path() /
        "platevision_m1_roundtrip.mp4";
    std::string error;
    CHECK(generateSampleVideo(
        video_path,
        320,
        240,
        4,
        10.0,
        error));

    std::unique_ptr<IVideoSource> source =
        createOpenCvMediaSource(video_path, error);
    CHECK(source != nullptr);

    int frame_count = 0;
    Frame frame;
    while (source->read(frame, std::chrono::milliseconds(100))) {
        CHECK(!frame.bgr.empty());
        ++frame_count;
    }
    source->close();
    std::filesystem::remove(video_path);
    CHECK(frame_count == 4);
    return true;
}

bool testOnnxPipeline() {
#ifdef PLATEVISION_HAS_ONNX
    SymbolTable symbols;
    std::string error;
    CHECK(SymbolTable::loadFromFile(
        std::filesystem::u8path(PLATEVISION_CONFIG_DIR) /
            "plate_alphabet.txt",
        symbols,
        error));

    OnnxDetectorConfig detector_config;
    detector_config.model_path =
        std::filesystem::u8path(PLATEVISION_TEST_MODEL_DIR) /
        "detector_constant.onnx";
    std::unique_ptr<IPlateDetector> detector =
        createOnnxPlateDetector(detector_config, error);
    CHECK(detector != nullptr);

    OnnxRecognizerConfig recognizer_config;
    recognizer_config.model_path =
        std::filesystem::u8path(PLATEVISION_TEST_MODEL_DIR) /
        "recognizer_constant.onnx";
    recognizer_config.symbols = symbols;
    std::unique_ptr<IPlateRecognizer> recognizer =
        createOnnxPlateRecognizer(recognizer_config, error);
    CHECK(recognizer != nullptr);

    PerspectiveRectifier rectifier;
    SimpleIouTracker tracker;
    RecognitionPipeline pipeline(
        *detector,
        rectifier,
        *recognizer,
        tracker);

    cv::Mat image(1080, 1920, CV_8UC3, cv::Scalar(30, 50, 70));
    Frame frame;
    frame.bgr = image;

    FrameResult result;
    for (int index = 0; index < 3; ++index) {
        frame.sequence = static_cast<std::uint64_t>(index);
        frame.source_timestamp_us =
            static_cast<std::int64_t>(index + 1) * 1000;
        result = pipeline.process(std::make_shared<Frame>(frame));
    }

    CHECK(result.error.empty());
    CHECK(result.observations.size() == 1);
    CHECK(result.observations[0].valid);
    CHECK(result.observations[0].text == u8"京A12345");
    CHECK(result.observations[0].color == PlateColor::Blue);
    CHECK(result.tracks[0].confirmed);
    return true;
#else
    std::cout << "[  SKIPPED ] ONNX Runtime is disabled\n";
    return true;
#endif
}

bool testOnnxAlternativeLayouts() {
#ifdef PLATEVISION_HAS_ONNX
    SymbolTable symbols;
    std::string error;
    CHECK(SymbolTable::loadFromFile(
        std::filesystem::u8path(PLATEVISION_CONFIG_DIR) /
            "plate_alphabet.txt",
        symbols,
        error));

    OnnxDetectorConfig detector_config;
    detector_config.model_path =
        std::filesystem::u8path(PLATEVISION_TEST_MODEL_DIR) /
        "detector_detections_first.onnx";
    detector_config.output_layout =
        DetectionOutputLayout::DetectionsFirst;
    std::unique_ptr<IPlateDetector> detector =
        createOnnxPlateDetector(detector_config, error);
    CHECK(detector != nullptr);

    OnnxRecognizerConfig recognizer_config;
    recognizer_config.model_path =
        std::filesystem::u8path(PLATEVISION_TEST_MODEL_DIR) /
        "recognizer_class_major.onnx";
    recognizer_config.output_layout =
        RecognitionOutputLayout::ClassMajor;
    recognizer_config.symbols = symbols;
    std::unique_ptr<IPlateRecognizer> recognizer =
        createOnnxPlateRecognizer(recognizer_config, error);
    CHECK(recognizer != nullptr);

    PerspectiveRectifier rectifier;
    SimpleIouTracker tracker;
    RecognitionPipeline pipeline(
        *detector,
        rectifier,
        *recognizer,
        tracker);

    Frame frame;
    frame.bgr = cv::Mat(
        1080,
        1920,
        CV_8UC3,
        cv::Scalar(30, 50, 70));
    frame.sequence = 0;
    frame.source_timestamp_us = 1000;
    const FrameResult result =
        pipeline.process(std::make_shared<Frame>(frame));

    CHECK(result.error.empty());
    CHECK(result.observations.size() == 1);
    CHECK(result.observations[0].valid);
    CHECK(result.observations[0].text == u8"京A12345");
    return true;
#else
    return true;
#endif
}

bool testPlateEvaluator() {
#ifdef PLATEVISION_HAS_ONNX
    SymbolTable symbols;
    std::string error;
    CHECK(SymbolTable::loadFromFile(
        std::filesystem::u8path(PLATEVISION_CONFIG_DIR) /
            "plate_alphabet.txt",
        symbols,
        error));

    OnnxDetectorConfig detector_config;
    detector_config.model_path =
        std::filesystem::u8path(PLATEVISION_TEST_MODEL_DIR) /
        "detector_constant.onnx";
    std::unique_ptr<IPlateDetector> detector =
        createOnnxPlateDetector(detector_config, error);
    CHECK(detector != nullptr);

    OnnxRecognizerConfig recognizer_config;
    recognizer_config.model_path =
        std::filesystem::u8path(PLATEVISION_TEST_MODEL_DIR) /
        "recognizer_constant.onnx";
    recognizer_config.symbols = symbols;
    std::unique_ptr<IPlateRecognizer> recognizer =
        createOnnxPlateRecognizer(recognizer_config, error);
    CHECK(recognizer != nullptr);

    const std::filesystem::path image_path =
        std::filesystem::temp_directory_path() /
        "platevision_evaluation_sample.png";
    cv::Mat image(1080, 1920, CV_8UC3, cv::Scalar(30, 50, 70));
    cv::rectangle(
        image,
        cv::Rect(660, 604, 461, 81),
        cv::Scalar(245, 245, 245),
        cv::FILLED);
    CHECK(cv::imwrite(image_path.string(), image));

    PerspectiveRectifier rectifier;
    PlateEvaluator evaluator(*detector, rectifier, *recognizer);
    EvaluationSample sample;
    sample.image_path = image_path;
    sample.expected_plate = u8"京A12345";
    sample.expected_color = PlateColor::Blue;
    sample.expected_box = cv::Rect2f(660.0f, 450.0f, 600.0f, 180.0f);
    const EvaluationResult result = evaluator.evaluate(sample);
    std::filesystem::remove(image_path);

    CHECK(result.error.empty());
    CHECK(result.image_loaded);
    CHECK(result.detection_found);
    CHECK(result.detection_iou.has_value());
    CHECK(*result.detection_iou > 0.99f);
    CHECK(result.exact_match);
    CHECK(result.valid);
    CHECK(std::abs(result.character_accuracy - 1.0) < 1e-9);
    CHECK(result.latency_us > 0);
    return true;
#else
    return true;
#endif
}

bool testHyperLprRealModels() {
#ifdef PLATEVISION_HAS_ONNX
    const std::filesystem::path detector_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_MODEL_DIR) /
        "y5fu_640x_sim.onnx";
    const std::filesystem::path recognizer_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_MODEL_DIR) /
        "rpv3_mdict_160_r3.onnx";
    const std::filesystem::path image_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_IMAGE);
    if (!std::filesystem::exists(detector_path) ||
        !std::filesystem::exists(recognizer_path) ||
        !std::filesystem::exists(image_path)) {
        std::cout << "[  SKIPPED ] HyperLPR model files not found\n";
        return true;
    }

    SymbolTable symbols;
    std::string error;
    CHECK(SymbolTable::loadFromFile(
        std::filesystem::u8path(PLATEVISION_CONFIG_DIR) /
            "hyperlpr_tokens.txt",
        symbols,
        error));

    HyperLprDetectorConfig detector_config;
    detector_config.model_path = detector_path;
    detector_config.score_threshold = 0.30f;
    detector_config.iou_threshold = 0.60f;
    std::unique_ptr<IPlateDetector> detector =
        createHyperLprPlateDetector(detector_config, error);
    CHECK(detector != nullptr);

    HyperLprRecognizerConfig recognizer_config;
    recognizer_config.model_path = recognizer_path;
    recognizer_config.classifier_model_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_MODEL_DIR) /
        "litemodel_cls_96x_r1.onnx";
    recognizer_config.symbols = symbols;
    std::unique_ptr<IPlateRecognizer> recognizer =
        createHyperLprPlateRecognizer(recognizer_config, error);
    CHECK(recognizer != nullptr);

    PerspectiveRectifier rectifier;
    PlateEvaluator evaluator(*detector, rectifier, *recognizer);
    EvaluationSample sample;
    sample.image_path = image_path;
    sample.expected_plate = u8"苏ED51712";
    sample.expected_color = PlateColor::Green;
    const EvaluationResult result = evaluator.evaluate(sample);

    CHECK(result.error.empty());
    CHECK(result.detection_found);
    CHECK(result.valid);
    CHECK(result.predicted_plate == u8"苏ED51712");
    CHECK(result.exact_match);
    CHECK(std::abs(result.character_accuracy - 1.0) < 1e-9);
    return true;
#else
    return true;
#endif
}

bool testHyperLprCropEvaluator() {
#ifdef PLATEVISION_HAS_ONNX
    const std::filesystem::path recognizer_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_MODEL_DIR) /
        "rpv3_mdict_160_r3.onnx";
    const std::filesystem::path image_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_IMAGE).parent_path() /
        "rec_crop" /
        u8"_0_津B6H920.jpg";
    if (!std::filesystem::exists(recognizer_path) ||
        !std::filesystem::exists(image_path)) {
        std::cout << "[  SKIPPED ] HyperLPR crop assets not found\n";
        return true;
    }

    SymbolTable symbols;
    std::string error;
    CHECK(SymbolTable::loadFromFile(
        std::filesystem::u8path(PLATEVISION_CONFIG_DIR) /
            "hyperlpr_tokens.txt",
        symbols,
        error));
    HyperLprRecognizerConfig config;
    config.model_path = recognizer_path;
    config.classifier_model_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_MODEL_DIR) /
        "litemodel_cls_96x_r1.onnx";
    config.symbols = symbols;
    std::unique_ptr<IPlateRecognizer> recognizer =
        createHyperLprPlateRecognizer(config, error);
    CHECK(recognizer != nullptr);

    CropPlateEvaluator evaluator(*recognizer);
    EvaluationSample sample;
    sample.image_path = image_path;
    sample.expected_plate = u8"津B6H920";
    sample.expected_color = PlateColor::Blue;
    const EvaluationResult result = evaluator.evaluate(sample);
    CHECK(result.error.empty());
    CHECK(result.exact_match);
    CHECK(result.predicted_plate == u8"津B6H920");
    return true;
#else
    return true;
#endif
}

bool testDirectMLProvider() {
#if defined(PLATEVISION_HAS_ONNX) && defined(PLATEVISION_HAS_DIRECTML)
    SymbolTable symbols;
    std::string error;
    CHECK(SymbolTable::loadFromFile(
        std::filesystem::u8path(PLATEVISION_CONFIG_DIR) /
            "plate_alphabet.txt",
        symbols,
        error));

    OnnxRecognizerConfig config;
    config.model_path =
        std::filesystem::u8path(PLATEVISION_TEST_MODEL_DIR) /
        "recognizer_constant.onnx";
    config.symbols = symbols;
    config.provider = ExecutionProvider::DirectML;
    std::unique_ptr<IPlateRecognizer> recognizer =
        createOnnxPlateRecognizer(config, error);
    if (!recognizer) {
        std::cout << "[  SKIPPED ] DirectML unavailable: "
                  << error << "\n";
        return true;
    }

    const std::filesystem::path image_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_IMAGE).parent_path() /
        "rec_crop" /
        u8"_0_津B6H920.jpg";
    if (!std::filesystem::exists(image_path)) {
        return true;
    }
    CropPlateEvaluator evaluator(*recognizer);
    EvaluationSample sample;
    sample.image_path = image_path;
    sample.expected_plate = u8"京A12345";
    sample.expected_color = PlateColor::Blue;
    const EvaluationResult result = evaluator.evaluate(sample);
    CHECK(result.error.empty());
    CHECK(result.exact_match);
    return true;
#else
    return true;
#endif
}

bool testHyperLprYellowClassifier() {
#ifdef PLATEVISION_HAS_ONNX
    const std::filesystem::path recognizer_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_MODEL_DIR) /
        "rpv3_mdict_160_r3.onnx";
    const std::filesystem::path classifier_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_MODEL_DIR) /
        "litemodel_cls_96x_r1.onnx";
    const std::filesystem::path image_path =
        std::filesystem::u8path(PLATEVISION_HYPERLPR_IMAGE).parent_path() /
        "yellow_crop.png";
    if (!std::filesystem::exists(recognizer_path) ||
        !std::filesystem::exists(classifier_path) ||
        !std::filesystem::exists(image_path)) {
        std::cout << "[  SKIPPED ] HyperLPR yellow fixture not found\n";
        return true;
    }

    SymbolTable symbols;
    std::string error;
    CHECK(SymbolTable::loadFromFile(
        std::filesystem::u8path(PLATEVISION_CONFIG_DIR) /
            "hyperlpr_tokens.txt",
        symbols,
        error));
    HyperLprRecognizerConfig config;
    config.model_path = recognizer_path;
    config.classifier_model_path = classifier_path;
    config.symbols = symbols;
    std::unique_ptr<IPlateRecognizer> recognizer =
        createHyperLprPlateRecognizer(config, error);
    CHECK(recognizer != nullptr);

    CropPlateEvaluator evaluator(*recognizer);
    EvaluationSample sample;
    sample.image_path = image_path;
    const EvaluationResult result = evaluator.evaluate(sample);
    CHECK(result.error.empty());
    CHECK(result.predicted_color == PlateColor::Yellow);
    return true;
#else
    return true;
#endif
}

void runTest(const char* name, bool (*test)()) {
    std::cout << "[ RUN      ] " << name << "\n";
    if (test()) {
        std::cout << "[       OK ] " << name << "\n";
    } else {
        std::cout << "[  FAILED  ] " << name << "\n";
        ++failures;
    }
}

}  // namespace

int main() {
    runTest("BoundedQueueDropsOldest", testBoundedQueueDropsOldest);
    runTest("BoundedQueueClose", testBoundedQueueClose);
    runTest("PlaybackController", testPlaybackController);
    runTest("PlateValidator", testPlateValidator);
    runTest("PlateMetrics", testPlateMetrics);
    runTest("StableTextVoter", testStableTextVoter);
    runTest("DuplicateEventFilter", testDuplicateEventFilter);
    runTest("EventCoordinator", testEventCoordinator);
    runTest("RecordServicePersistence", testRecordServicePersistence);
    runTest("RecognitionPipeline", testRecognitionPipeline);
    runTest("CaptureService", testCaptureService);
    runTest("OpenCvVideoRoundTrip", testOpenCvVideoRoundTrip);
    runTest("OnnxPipeline", testOnnxPipeline);
    runTest(
        "OnnxAlternativeLayouts",
        testOnnxAlternativeLayouts);
    runTest("PlateEvaluator", testPlateEvaluator);
    runTest("HyperLprRealModels", testHyperLprRealModels);
    runTest("HyperLprCropEvaluator", testHyperLprCropEvaluator);
    runTest("DirectMLProvider", testDirectMLProvider);
    runTest("HyperLprYellowClassifier", testHyperLprYellowClassifier);

    if (failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed.\n";
    return 1;
}

