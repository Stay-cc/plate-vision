cmake_minimum_required(VERSION 3.24)

project(PlateVisionDesktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(QT_ROOT "D:/DownLoad/Qt/6.12.0/mingw_64" CACHE PATH "Qt installation")
list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")

find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets Sql)

qt_standard_project_setup()

add_executable(plate_desktop
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
)

target_link_libraries(plate_desktop
    PRIVATE
        Qt6::Widgets
        Qt6::Sql)

target_compile_definitions(plate_desktop
    PRIVATE
        PLATEVISION_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")

target_compile_options(plate_desktop
    PRIVATE
        -finput-charset=UTF-8
        -fexec-charset=UTF-8)

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${QT_ROOT}/bin"
        REQUIRED)
    add_custom_command(TARGET plate_desktop POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations
            "$<TARGET_FILE:plate_desktop>"
        COMMENT "Deploying Qt runtime")
endif()

