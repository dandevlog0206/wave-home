#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-debug"
OUTPUT_DIR="${ROOT}/bin-debug"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
JOBS="${JOBS:-$(nproc)}"

cmake -S "${ROOT}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DWAVE_BUILD_NCNN=ON \
  -DWAVE_RUNTIME_OUTPUT_DIR="${OUTPUT_DIR}"

cmake --build "${BUILD_DIR}" -j"${JOBS}"

echo "Debug server binary: ${OUTPUT_DIR}/wave-server"
