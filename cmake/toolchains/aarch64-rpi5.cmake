# Cross-compile for Raspberry Pi 5 (aarch64 Linux) — use later, not for daily dev.
#
# Native dev (this Ryzen x64 box):
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j$(nproc)
#
# RPi5 cross build (when ready):
#   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#   cmake -B build-rpi5 -DCMAKE_BUILD_TYPE=Release -DWAVE_CROSS_RPI5=ON
#   cmake --build build-rpi5 -j$(nproc)
#
# Optional: point CMAKE_SYSROOT at a Pi rootfs for linked libs (OpenSSL, etc.).

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Optional sysroot (uncomment and set when linking against Pi rootfs libs)
# set(CMAKE_SYSROOT /path/to/rpi-sysroot)
# set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
# set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
# set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
