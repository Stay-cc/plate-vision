add_executable(platevision_tests
    src/test_main.cpp
)

target_link_libraries(platevision_tests
    PRIVATE
        PlateVision::Core
        PlateVision::Evaluation
        PlateVision::Storage
        PlateVision::Video)

target_compile_definitions(platevision_tests
    PRIVATE
        PLATEVISION_CONFIG_DIR="${CMAKE_SOURCE_DIR}/configs"
        PLATEVISION_TEST_MODEL_DIR="${CMAKE_SOURCE_DIR}/tests/models"
        PLATEVISION_HYPERLPR_MODEL_DIR="${CMAKE_SOURCE_DIR}/third_party/hyperlpr/models/20230229/onnx"
        PLATEVISION_HYPERLPR_IMAGE="${CMAKE_SOURCE_DIR}/third_party/hyperlpr/images/test_img.jpg")

if(PLATEVISION_HAS_ONNX)
    target_link_libraries(
        platevision_tests PRIVATE PlateVision::Inference)
    target_compile_definitions(
        platevision_tests PRIVATE PLATEVISION_HAS_ONNX=1)
endif()

copy_opencv_runtime(platevision_tests)
copy_onnxruntime(platevision_tests)

add_test(
    NAME platevision_tests
    COMMAND platevision_tests)

