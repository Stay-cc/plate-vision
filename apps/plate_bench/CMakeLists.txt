add_executable(plate_bench
    src/main.cpp
)

target_link_libraries(plate_bench
    PRIVATE
        PlateVision::Core
        PlateVision::Evaluation
        PlateVision::Inference)

target_compile_definitions(plate_bench
    PRIVATE
        PLATEVISION_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs"
        PLATEVISION_HAS_ONNX=1)

copy_opencv_runtime(plate_bench)
copy_onnxruntime(plate_bench)

