add_library(platevision_core STATIC
    src/capture_service.cpp
    src/duplicate_event_filter.cpp
    src/event_coordinator.cpp
    src/mock_adapters.cpp
    src/plate_validator.cpp
    src/playback_controller.cpp
    src/recognition_pipeline.cpp
    src/simple_iou_tracker.cpp
    src/stable_text_voter.cpp
)

add_library(PlateVision::Core ALIAS platevision_core)

target_include_directories(platevision_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(platevision_core
    PUBLIC
        opencv_core
        opencv_imgproc)

target_compile_definitions(platevision_core
    PUBLIC
        NOMINMAX)

