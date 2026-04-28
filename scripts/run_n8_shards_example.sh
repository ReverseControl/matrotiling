#!/usr/bin/env bash
set -euo pipefail

# NUMA/process sharding example for n=8 production state-cache search.
# Prefix-task sharding is usually better balanced; this root-shard script is kept
# for simple experiments.

SHARDS="${SHARDS:-8}"
THREADS_PER_SHARD="${THREADS_PER_SHARD:-12}"
DATA_DIR="${DATA_DIR:-data}"
CACHE_DIR="${CACHE_DIR:-.alexeev_cache}"
OUT_DIR="${OUT_DIR:-shards}"
STATE_MB_PER_SHARD="${STATE_MB_PER_SHARD:-8192}"
COMPAT_MB_PER_SHARD="${COMPAT_MB_PER_SHARD:-8192}"
MEM_LIMIT_MB_PER_SHARD="${MEM_LIMIT_MB_PER_SHARD:-48000}"

mkdir -p "${OUT_DIR}" "${CACHE_DIR}"

for ((i=0; i<SHARDS; ++i)); do
  node=$((i % 2))
  cmd=(./build/alexeev_r3_cpp 8
      --data-dir "${DATA_DIR}"
      --cache-dir "${CACHE_DIR}/shard_${i}"
      --generation-mode state-cache
      --state-cache bounded
      --state-cache-mb "${STATE_MB_PER_SHARD}"
      --compat-mem-budget-mb "${COMPAT_MB_PER_SHARD}"
      --compat-cache-policy lru
      --memory-limit-mb "${MEM_LIMIT_MB_PER_SHARD}"
      --memory-policy shrink
      --solution-dedup memory
      --search-mode quotient
      --volume-mode sha
      --threads "${THREADS_PER_SHARD}"
      --scheduler static
      --task-target-per-thread 64
      --split-max-depth 4
      --split-min-candidates 16
      --force-propagation
      --async-output
      --flush-every 1024
      --root-shard "${i}/${SHARDS}"
      --jsonl
      --out "${OUT_DIR}/r3n8_shard_${i}.jsonl")

  if command -v numactl >/dev/null 2>&1; then
    numactl --cpunodebind="${node}" --membind="${node}" "${cmd[@]}" &
  else
    "${cmd[@]}" &
  fi
done

wait
python3 scripts/merge_jsonl_by_key.py "${OUT_DIR}/r3n8_merged.jsonl" "${OUT_DIR}"/r3n8_shard_*.jsonl
