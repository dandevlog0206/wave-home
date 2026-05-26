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
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)

# Prefer Ubuntu/Debian multiarch target paths on the host.
set(CMAKE_FIND_ROOT_PATH
    /usr
    /usr/aarch64-linux-gnu)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Keep pkg-config aligned with the ARM64 target when a package uses it.
set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_LIBDIR}
    "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig:/usr/aarch64-linux-gnu/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/")

# Optional sysroot (for a real Pi rootfs, replace the roots above)
# set(CMAKE_SYSROOT /path/to/rpi-sysroot)
