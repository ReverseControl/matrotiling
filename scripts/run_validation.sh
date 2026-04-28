#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/build/alexeev_r3_cpp"
THREADS="${THREADS:-1}"
if [[ ! -x "$BIN" ]]; then
  cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build" -j
fi
"$BIN" 4 --data-dir "$ROOT/data" --validate --count-only --threads "$THREADS" --no-memory-plan
"$BIN" 5 --data-dir "$ROOT/data" --validate --count-only --threads "$THREADS" --no-memory-plan
"$BIN" 6 --data-dir "$ROOT/data" --validate --count-only --threads "$THREADS" --no-memory-plan
echo "Validation completed with THREADS=${THREADS}."
