#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"

echo "[1/3] Building web site into ${ROOT}/site"
"${ROOT}/script/build_site.sh"

echo "[2/3] Configuring C++ build"
cmake -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DWAVE_BUILD_NCNN=ON

echo "[3/3] Building wave-server"
cmake --build "${BUILD_DIR}" -j"${JOBS}"

echo
echo "Pi 5 build complete."
echo "  site:   ${ROOT}/site"
echo "  server: ${ROOT}/bin/wave-server"
