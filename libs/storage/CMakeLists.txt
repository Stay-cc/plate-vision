add_library(platevision_storage STATIC
    src/record_service.cpp
    src/snapshot_writer.cpp
    src/sqlite_recognition_repository.cpp
)

add_library(PlateVision::Storage ALIAS platevision_storage)

target_include_directories(platevision_storage
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(platevision_storage
    PUBLIC
        PlateVision::Core
    PRIVATE
        winsqlite3
        opencv_imgcodecs
        opencv_imgproc)

