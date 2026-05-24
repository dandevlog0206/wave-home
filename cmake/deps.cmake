# Ensure drogon's trantor submodule is present (git submodule update --init --recursive).
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/drogon/trantor/CMakeLists.txt")
    message(FATAL_ERROR
        "Missing thirdparty/drogon/trantor. Run:\n"
        "  git submodule update --init --recursive thirdparty/drogon")
endif()

if(WAVE_BUILD_NCNN AND NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/asio/asio/include/asio.hpp")
    message(FATAL_ERROR
        "Missing thirdparty/asio. Run:\n"
        "  git submodule update --init thirdparty/asio")
endif()
