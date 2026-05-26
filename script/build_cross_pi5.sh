#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build_pi5"
PACKAGE_DIR="${BUILD_DIR}/package_root"
TARBALL="${BUILD_DIR}/wave-home-pi5.tar.gz"
TOOLCHAIN_FILE="${ROOT}/cmake/toolchains/aarch64-rpi5.cmake"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: required command not found: $1" >&2
    exit 1
  fi
}

require_cmd cmake
require_cmd dpkg
require_cmd npm
require_cmd tar
require_cmd aarch64-linux-gnu-g++
require_cmd aarch64-linux-gnu-gcc

require_arm64_deps() {
  local foreign_arches
  foreign_arches="$(dpkg --print-foreign-architectures)"
  case " ${foreign_arches} " in
    *" arm64 "*) ;;
    *)
      cat >&2 <<'EOF'
error: arm64 multiarch is not enabled.

Run:
  sudo dpkg --add-architecture arm64
  sudo apt update
EOF
      exit 1
      ;;
  esac

  local missing=()
  local pkg
  for pkg in libjsoncpp-dev:arm64 libssl-dev:arm64 uuid-dev:arm64 zlib1g-dev:arm64; do
    if ! dpkg -s "${pkg}" >/dev/null 2>&1; then
      missing+=("${pkg}")
    fi
  done

  if [ "${#missing[@]}" -gt 0 ]; then
    printf 'error: missing ARM64 development packages:' >&2
    printf ' %s' "${missing[@]}" >&2
    printf '\n\nRun:\n  sudo apt install' >&2
    printf ' %s' "${missing[@]}" >&2
    printf '\n' >&2
    exit 1
  fi
}

require_arm64_deps

rm -rf "${PACKAGE_DIR}" "${TARBALL}"
mkdir -p "${PACKAGE_DIR}/bin"

echo "[1/4] Building web site into ${ROOT}/site"
"${ROOT}/script/build_site.sh"

echo "[2/4] Configuring Raspberry Pi 5 cross-build in ${BUILD_DIR}"
cmake -S "${ROOT}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
  -DWAVE_CROSS_RPI5=ON \
  -DWAVE_BUILD_NCNN=ON \
  -DWAVE_RUNTIME_OUTPUT_DIR="${PACKAGE_DIR}/bin" \
  -DWAVE_RUNTIME_OUTPUT_NAME="wave-home"

echo "[3/4] Building wave-home"
cmake --build "${BUILD_DIR}" -j"${JOBS}"

echo "[4/4] Staging package contents"
cp -a "${ROOT}/site" "${PACKAGE_DIR}/site"
cp -a "${ROOT}/gesture_set" "${PACKAGE_DIR}/gesture_set"

tar -C "${PACKAGE_DIR}" -czf "${TARBALL}" \
  bin \
  site \
  gesture_set

echo
echo "Cross-build complete."
echo "  build:   ${BUILD_DIR}"
echo "  package: ${TARBALL}"
