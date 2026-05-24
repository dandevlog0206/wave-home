cmake_minimum_required(VERSION 3.16)

# Toolchain must be selected before project().
option(WAVE_CROSS_RPI5 "Cross-compile for Raspberry Pi 5 (aarch64)" OFF)
if(WAVE_CROSS_RPI5 AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchains/aarch64-rpi5.cmake"
        CACHE FILEPATH "RPi5 cross toolchain" FORCE)
endif()

project(wave-home VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WAVE_BUILD_NCNN "Build Tencent ncnn third-party library" ON)

if(WAVE_CROSS_RPI5)
    message(STATUS "wave-home: cross-compiling for aarch64 (Raspberry Pi 5)")
else()
    message(STATUS "wave-home: native build (${CMAKE_SYSTEM_PROCESSOR})")
endif()

include("${CMAKE_SOURCE_DIR}/cmake/deps.cmake")
if(WAVE_BUILD_NCNN)
    include("${CMAKE_SOURCE_DIR}/cmake/asio.cmake")
endif()

# --- Drogon: disable unused components ---
set(BUILD_CTL OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_ORM OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_YAML_CONFIG OFF CACHE BOOL "" FORCE)
set(COZ_PROFILING OFF CACHE BOOL "" FORCE)
set(USE_SUBMODULE ON CACHE BOOL "" FORCE)

add_subdirectory(thirdparty/drogon)

# --- nlohmann/json (header-only, no tests/install) ---
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(JSON_CI OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/json)

# --- Tencent ncnn (gesture inference; optional until linked from app code) ---
if(WAVE_BUILD_NCNN)
    set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)
    set(NCNN_PYTHON OFF CACHE BOOL "" FORCE)
    set(NCNN_VULKAN OFF CACHE BOOL "" FORCE)
    add_subdirectory(thirdparty/ncnn)
endif()

add_subdirectory(src)
