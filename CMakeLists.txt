cmake_minimum_required(VERSION 3.24)

project(PlateVision VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper}
        "${CMAKE_BINARY_DIR}/bin/${config}")
endforeach()

if(NOT OpenCV_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/build")
    set(OpenCV_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/build"
        CACHE PATH "OpenCV build directory")
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)

set(ONNXRUNTIME_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime-directml"
    CACHE PATH "ONNX Runtime package root")

set(PLATEVISION_ENABLE_ONNX ON CACHE BOOL "Enable ONNX Runtime inference")
set(PLATEVISION_HAS_ONNX OFF)
set(PLATEVISION_HAS_DIRECTML OFF)

if(PLATEVISION_ENABLE_ONNX)
    set(ONNXRUNTIME_DLL
        "${ONNXRUNTIME_ROOT}/runtimes/win-x64/native/onnxruntime.dll")
    set(ONNXRUNTIME_IMPLIB
        "${ONNXRUNTIME_ROOT}/runtimes/win-x64/native/onnxruntime.lib")
    set(ONNXRUNTIME_INCLUDE
        "${ONNXRUNTIME_ROOT}/build/native/include")

    if(EXISTS "${ONNXRUNTIME_DLL}" AND
       EXISTS "${ONNXRUNTIME_IMPLIB}" AND
       EXISTS "${ONNXRUNTIME_INCLUDE}")
        add_library(onnxruntime SHARED IMPORTED GLOBAL)
        set_target_properties(onnxruntime PROPERTIES
            IMPORTED_LOCATION "${ONNXRUNTIME_DLL}"
            IMPORTED_IMPLIB "${ONNXRUNTIME_IMPLIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE}")
        set(PLATEVISION_HAS_ONNX ON)
        if(EXISTS "${ONNXRUNTIME_INCLUDE}/dml_provider_factory.h")
            set(PLATEVISION_HAS_DIRECTML ON)
        endif()
    else()
        message(WARNING
            "ONNX Runtime package was not found at ${ONNXRUNTIME_ROOT}; "
            "building without ONNX support")
    endif()
endif()

function(copy_opencv_runtime target)
    if(WIN32)
        file(GLOB OPENCV_RUNTIME_DLLS
            "${OpenCV_DIR}/x64/vc16/bin/opencv_world*.dll"
            "${OpenCV_DIR}/x64/vc17/bin/opencv_world*.dll"
            "${OpenCV_DIR}/x64/vc16/bin/opencv_videoio_ffmpeg*.dll"
            "${OpenCV_DIR}/x64/vc17/bin/opencv_videoio_ffmpeg*.dll")
        if(OPENCV_RUNTIME_DLLS)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${OPENCV_RUNTIME_DLLS}
                    "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        endif()
    endif()
endfunction()

function(copy_onnxruntime target)
    if(WIN32 AND PLATEVISION_HAS_ONNX)
        set(ONNX_RUNTIME_DEPENDENCIES "${ONNXRUNTIME_DLL}")
        if(EXISTS "${ONNXRUNTIME_ROOT}/runtimes/win-x64/native/onnxruntime_providers_shared.dll")
            list(APPEND ONNX_RUNTIME_DEPENDENCIES
                "${ONNXRUNTIME_ROOT}/runtimes/win-x64/native/onnxruntime_providers_shared.dll")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${ONNX_RUNTIME_DEPENDENCIES}
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()

add_subdirectory(libs/core)
add_subdirectory(libs/evaluation)
add_subdirectory(libs/storage)
if(PLATEVISION_HAS_ONNX)
    add_subdirectory(libs/inference)
endif()
add_subdirectory(libs/video)
add_subdirectory(apps/plate_cli)
add_subdirectory(apps/plate_records)
if(PLATEVISION_HAS_ONNX)
    add_subdirectory(apps/plate_bench)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

