add_library(platevision_video STATIC
    src/opencv_sources.cpp
    src/sample_generator.cpp
)

add_library(PlateVision::Video ALIAS platevision_video)

target_include_directories(platevision_video
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(platevision_video
    PUBLIC
        PlateVision::Core
        ${OpenCV_LIBS})

