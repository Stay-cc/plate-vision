add_library(platevision_evaluation STATIC
    src/plate_evaluator.cpp
    src/plate_metrics.cpp
)

add_library(PlateVision::Evaluation ALIAS platevision_evaluation)

target_include_directories(platevision_evaluation
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(platevision_evaluation
    PUBLIC
        PlateVision::Core
        opencv_imgcodecs)

