cmake_minimum_required(VERSION 3.24)

project(carve LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CarveCompileOptions)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

option(CARVE_ENABLE_CLANG_TIDY "Enable clang-tidy during compilation." OFF)
set(CARVE_CLANG_TIDY_CHECKS "" CACHE STRING "Override clang-tidy checks list.")

find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CARVE_ENABLE_CLANG_TIDY)
    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR "CARVE_ENABLE_CLANG_TIDY is ON but clang-tidy was not found.")
    endif()

    set(_carve_clang_tidy_args "${CLANG_TIDY_EXE}")
    if(CARVE_CLANG_TIDY_CHECKS)
        list(APPEND _carve_clang_tidy_args "--checks=${CARVE_CLANG_TIDY_CHECKS}")
    endif()
    list(APPEND _carve_clang_tidy_args "--header-filter=^${CMAKE_SOURCE_DIR}/src/.*")
    set(CMAKE_CXX_CLANG_TIDY "${_carve_clang_tidy_args}")
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
find_package(bgfx REQUIRED CONFIG)

if(TARGET bgfx::bgfx)
    set(BGFX_TARGET bgfx::bgfx)
elseif(TARGET bgfx)
    set(BGFX_TARGET bgfx)
else()
    message(FATAL_ERROR "No usable bgfx CMake target found.")
endif()

set(CARVE_SOURCES
    src/main.cpp
    src/scene/SceneCore.cpp
    src/scene/SceneCore.h
    src/render/RenderBridge.cpp
    src/render/RenderBridge.h
    src/ui/BgfxViewWidget.cpp
    src/ui/BgfxViewWidget.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
)

add_executable(carve ${CARVE_SOURCES})

set(CARVE_TIDY_SOURCES
    src/main.cpp
    src/scene/SceneCore.cpp
    src/render/RenderBridge.cpp
    src/ui/BgfxViewWidget.cpp
    src/ui/MainWindow.cpp
)

target_include_directories(carve
    PRIVATE
        src
)

target_compile_definitions(carve
    PRIVATE
        CARVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(carve
    PRIVATE
        Qt6::Widgets
        ${BGFX_TARGET}
)

if(WIN32)
    target_link_libraries(carve PRIVATE gdi32)
endif()

add_custom_command(TARGET carve POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/render/shaders"
        "$<TARGET_FILE_DIR:carve>/shaders"
)

if(CLANG_TIDY_EXE)
    add_custom_target(clang-tidy
        COMMAND "${CLANG_TIDY_EXE}" -p="${CMAKE_BINARY_DIR}" "--header-filter=^${CMAKE_SOURCE_DIR}/src/.*" ${CARVE_TIDY_SOURCES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running clang-tidy for carve sources"
        VERBATIM
    )
endif()
