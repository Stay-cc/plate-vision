add_library(platevision_inference STATIC
    src/onnx_adapters.cpp
    src/symbol_table.cpp
)

add_library(PlateVision::Inference ALIAS platevision_inference)

target_include_directories(platevision_inference
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(platevision_inference
    PUBLIC
        PlateVision::Core
        onnxruntime
        ${OpenCV_LIBS})

target_compile_definitions(platevision_inference
    PUBLIC
        PLATEVISION_HAS_ONNX=1)

if(PLATEVISION_HAS_DIRECTML)
    target_compile_definitions(platevision_inference
        PUBLIC
            PLATEVISION_HAS_DIRECTML=1)
endif()

