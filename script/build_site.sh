#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REACT_DIR="${ROOT}/react"
SITE_DIR="${ROOT}/site"

cd "${REACT_DIR}"

if [[ -f package-lock.json ]]; then
  npm ci
else
  npm install
fi

npm run build

mkdir -p "${SITE_DIR}"
rm -rf "${SITE_DIR:?}/"*
cp -a dist/. "${SITE_DIR}/"

echo "Site built into ${SITE_DIR}"
