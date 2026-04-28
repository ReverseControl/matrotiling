#!/usr/bin/env bash
set -euo pipefail

# Example Delta(3,9) commands.  This requires a full data/allr3n9.txt file.
# The distributed package includes only a README placeholder for that file.

BIN="${BIN:-./build/alexeev_r3_cpp}"
DATA_DIR="${DATA_DIR:-data}"
CACHE_DIR="${CACHE_DIR:-.alexeev_cache_n9}"

if [[ ! -f "${DATA_DIR}/allr3n9.txt" ]]; then
  echo "Missing ${DATA_DIR}/allr3n9.txt"
  echo "Generate or supply the n=9 rank-3 orbit reference file first."
  exit 1
fi

echo "== Validate/build n=9 wide image data =="
"${BIN}" 9 --data-dir "${DATA_DIR}" --cache-dir "${CACHE_DIR}" \
  --auto --validate --build-cache-only

echo "== Gather initial n=9 state-throughput data =="
"${BIN}" 9 --data-dir "${DATA_DIR}" --cache-dir "${CACHE_DIR}" \
  --auto --gather-data --gather-matrix "${GATHER_MATRIX:-core}" \
  --gather-states "${GATHER_STATES:-100000}" --gather-solutions 0

echo "== Optional n=9 bounded search =="
"${BIN}" 9 --data-dir "${DATA_DIR}" --cache-dir "${CACHE_DIR}" \
  --auto --auto-profile throughput \
  --state-cache bounded --state-cache-mb "${STATE_MB:-65536}" \
  --compat-mem-budget-mb "${COMPAT_MB:-65536}" \
  --progress-interval "${PROGRESS:-1000000}" \
  --out "${OUT:-r3n9_tilings.txt}"
