add_executable(plate_cli
    src/main.cpp
)

target_link_libraries(plate_cli
    PRIVATE
        PlateVision::Core
        PlateVision::Storage
        PlateVision::Video)

target_compile_definitions(plate_cli
    PRIVATE
        PLATEVISION_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs")

if(PLATEVISION_HAS_ONNX)
    target_link_libraries(plate_cli PRIVATE PlateVision::Inference)
    target_compile_definitions(plate_cli PRIVATE PLATEVISION_HAS_ONNX=1)
endif()

copy_opencv_runtime(plate_cli)
copy_onnxruntime(plate_cli)

