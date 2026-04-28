#!/usr/bin/env bash
set -euo pipefail
# Usage:
#   ./scripts/run_gather_data_example.sh 8 10000
# Writes perf_data.txt in the current working directory.
N="${1:-8}"
SOLUTIONS="${2:-10000}"
BIN="${BIN:-./build/alexeev_r3_cpp}"
DATA_DIR="${DATA_DIR:-data}"
CACHE_DIR="${CACHE_DIR:-.alexeev_cache}"

"$BIN" "$N" --data-dir "$DATA_DIR" --cache-dir "$CACHE_DIR"   --auto --gather-data --gather-solutions "$SOLUTIONS"
