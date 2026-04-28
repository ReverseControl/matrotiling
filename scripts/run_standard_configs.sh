#!/usr/bin/env bash
set -euo pipefail

# Example launcher for common hardware configurations.
# Usage:
#   CONFIG=auto ./scripts/run_standard_configs.sh 8
#   CONFIG=throughput ./scripts/run_standard_configs.sh 8
#   CONFIG=32c64g ./scripts/run_standard_configs.sh 8
#   CONFIG=192c512g ./scripts/run_standard_configs.sh 8
#   CONFIG=prefix192 ./scripts/run_standard_configs.sh 8

N="${1:-8}"
CONFIG="${CONFIG:-auto}"
DATA_DIR="${DATA_DIR:-data}"
CACHE_DIR="${CACHE_DIR:-.alexeev_cache}"
OUT="${OUT:-r3n${N}_tilings.txt}"

case "${CONFIG}" in
  auto)
    exec ./build/alexeev_r3_cpp "${N}" --data-dir "${DATA_DIR}" --cache-dir "${CACHE_DIR}" \
      --auto --auto-profile balanced --search-mode quotient --volume-mode sha --out "${OUT}"
    ;;
  throughput)
    exec ./build/alexeev_r3_cpp "${N}" --data-dir "${DATA_DIR}" --cache-dir "${CACHE_DIR}" \
      --auto --auto-profile throughput --search-mode quotient --volume-mode sha --out "${OUT}"
    ;;
  interactive)
    exec ./build/alexeev_r3_cpp "${N}" --data-dir "${DATA_DIR}" --cache-dir "${CACHE_DIR}" \
      --auto --auto-profile interactive --search-mode quotient --volume-mode sha --out "${OUT}"
    ;;
  32c64g)
    exec ./build/alexeev_r3_cpp "${N}" --data-dir "${DATA_DIR}" --cache-dir "${CACHE_DIR}" \
      --generation-mode state-cache --state-cache bounded --state-cache-mb 12288 \
      --compat-mem-budget-mb 12288 --memory-limit-mb 54000 --memory-policy shrink \
      --solution-dedup memory --threads 24 --parallel-depth 1 \
      --scheduler static --task-target-per-thread 64 --split-max-depth 4 --split-min-candidates 16 \
      --compat-cache-policy lru --prefix-task-target 0 --prefix-max-depth 2 \
      --force-propagation --async-output --flush-every 1024 \
      --search-mode quotient --volume-mode sha --out "${OUT}"
    ;;
  192c512g)
    exec ./build/alexeev_r3_cpp "${N}" --data-dir "${DATA_DIR}" --cache-dir "${CACHE_DIR}" \
      --generation-mode state-cache --state-cache bounded --state-cache-mb 65536 \
      --compat-mem-budget-mb 65536 --memory-limit-mb 430000 --memory-policy shrink \
      --solution-dedup memory --threads 96 --parallel-depth 1 \
      --scheduler static-hybrid --task-target-per-thread 64 --split-max-depth 4 --split-min-candidates 16 \
      --compat-cache-policy lru --prefix-task-target 0 --prefix-max-depth 2 \
      --force-propagation --async-output --flush-every 1024 \
      --search-mode quotient --volume-mode sha --progress-interval 1000000 \
      --out "${OUT}"
    ;;
  prefix192)
    PREFIX_FILE="${PREFIX_FILE:-n8_prefix_tasks.jsonl}"
    ./build/alexeev_r3_cpp 8 --data-dir "${DATA_DIR}" --cache-dir "${CACHE_DIR}" \
      --auto --auto-profile balanced --prefix-task-target 20000 --prefix-max-depth 2 \
      --write-prefix-tasks "${PREFIX_FILE}"
    SHARDS="${SHARDS:-8}" THREADS_PER_SHARD="${THREADS_PER_SHARD:-12}" \
    STATE_MB_PER_SHARD="${STATE_MB_PER_SHARD:-8192}" \
    COMPAT_MB_PER_SHARD="${COMPAT_MB_PER_SHARD:-8192}" \
    MEM_LIMIT_MB_PER_SHARD="${MEM_LIMIT_MB_PER_SHARD:-48000}" \
    DATA_DIR="${DATA_DIR}" ./scripts/run_n8_prefix_shards_example.sh "${PREFIX_FILE}"
    ;;
  *)
    echo "unknown CONFIG=${CONFIG}; use auto, throughput, interactive, 32c64g, 192c512g, or prefix192" >&2
    exit 2
    ;;
esac
