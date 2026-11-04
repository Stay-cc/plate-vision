#pragma once

#include "platevision/interfaces.hpp"
#include "platevision/plate_validator.hpp"

#include <string>

namespace platevision {

class RecognitionPipeline final {
public:
    RecognitionPipeline(
        IPlateDetector& detector,
        IPlateRectifier& rectifier,
        IPlateRecognizer& recognizer,
        IPlateTracker& tracker);

    FrameResult process(FrameRef frame);
    void reset();

private:
    IPlateDetector& detector_;
    IPlateRectifier& rectifier_;
    IPlateRecognizer& recognizer_;
    IPlateTracker& tracker_;
    PlateValidator validator_;
};

}  // namespace platevision

