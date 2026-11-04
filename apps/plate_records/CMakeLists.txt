add_executable(plate_records
    src/main.cpp
)

target_link_libraries(plate_records
    PRIVATE
        PlateVision::Storage)

copy_opencv_runtime(plate_records)

