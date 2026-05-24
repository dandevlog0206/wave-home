# Standalone Asio (header-only), vendored under thirdparty/asio.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/asio/asio/include/asio.hpp")
    message(FATAL_ERROR
        "Missing thirdparty/asio. Run:\n"
        "  git submodule update --init thirdparty/asio")
endif()

add_library(wave_asio INTERFACE)
target_include_directories(wave_asio SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/thirdparty/asio/asio/include")
target_compile_definitions(wave_asio INTERFACE ASIO_STANDALONE)

find_package(Threads REQUIRED)
target_link_libraries(wave_asio INTERFACE Threads::Threads)
