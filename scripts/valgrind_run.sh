#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
BIN="${BUILD_DIR}/volumectl"

if [[ ! -x "${BIN}" ]]; then
  echo "Binary not found or not executable: ${BIN}" >&2
  echo "Build first: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build" >&2
  exit 1
fi

LOG_DIR="${LOG_DIR:-${ROOT_DIR}/valgrind}"
mkdir -p "${LOG_DIR}"

LOG_FILE="${LOG_DIR}/volumectl.valgrind.log"

exec valgrind \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --errors-for-leak-kinds=definite,indirect,possible \
  --log-file="${LOG_FILE}" \
  "${BIN}" "$@"
