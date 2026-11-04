#pragma once

#include "platevision/types.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace platevision {

class IVideoSource {
public:
    virtual ~IVideoSource() = default;

    virtual bool open(std::string& error) = 0;
    virtual bool read(Frame& frame, std::chrono::milliseconds timeout) = 0;
    virtual void close() = 0;
};

class IPlateDetector {
public:
    virtual ~IPlateDetector() = default;

    virtual std::vector<PlateBox> detect(
        const Frame& frame,
        std::string& error) = 0;
};

class IPlateRectifier {
public:
    virtual ~IPlateRectifier() = default;

    virtual cv::Mat rectify(
        const Frame& frame,
        const PlateBox& box) = 0;
};

class IPlateRecognizer {
public:
    virtual ~IPlateRecognizer() = default;

    virtual PlateText recognize(
        const cv::Mat& plate_image,
        std::string& error) = 0;
};

class IPlateTracker {
public:
    virtual ~IPlateTracker() = default;

    virtual PlateTrack update(const PlateObservation& observation) = 0;
    virtual void reset() = 0;
};

class IRecognitionRepository {
public:
    virtual ~IRecognitionRepository() = default;

    virtual bool initialize(std::string& error) = 0;
    virtual bool save(
        const RecognitionEvent& event,
        std::string& error) = 0;
    virtual std::vector<RecognitionEvent> query(
        const RecordQuery& query,
        std::string& error) = 0;
};

}  // namespace platevision

