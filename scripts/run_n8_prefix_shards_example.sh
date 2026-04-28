#!/usr/bin/env bash
set -euo pipefail

PREFIX_FILE="${1:-n8_prefix_tasks.jsonl}"
SHARDS="${SHARDS:-8}"
THREADS_PER_SHARD="${THREADS_PER_SHARD:-12}"
STATE_MB_PER_SHARD="${STATE_MB_PER_SHARD:-16384}"
COMPAT_MB_PER_SHARD="${COMPAT_MB_PER_SHARD:-4096}"
MEM_LIMIT_MB_PER_SHARD="${MEM_LIMIT_MB_PER_SHARD:-52000}"
DATA_DIR="${DATA_DIR:-data}"
BIN="${BIN:-./build/alexeev_r3_cpp}"
OUT_DIR="${OUT_DIR:-n8_prefix_shards_out}"

mkdir -p "$OUT_DIR"

echo "Running $SHARDS weighted prefix shards from $PREFIX_FILE"
echo "threads/shard=$THREADS_PER_SHARD state_mb/shard=$STATE_MB_PER_SHARD compat_mb/shard=$COMPAT_MB_PER_SHARD"
echo "Set SHARDS, THREADS_PER_SHARD, STATE_MB_PER_SHARD, COMPAT_MB_PER_SHARD, MEM_LIMIT_MB_PER_SHARD to tune."
echo "When numactl is available, shards are alternated across NUMA nodes as a conservative dual-socket default."

for ((i=0; i<SHARDS; ++i)); do
  CACHE_DIR="$OUT_DIR/cache_$i"
  OUT_FILE="$OUT_DIR/n8_prefix_shard_${i}.jsonl"
  LOG_FILE="$OUT_DIR/n8_prefix_shard_${i}.log"

  CMD=("$BIN" 8
       --data-dir "$DATA_DIR"
       --cache-dir "$CACHE_DIR"
       --read-prefix-tasks "$PREFIX_FILE"
       --prefix-shard "$i/$SHARDS"
       --generation-mode state-cache
       --state-cache bounded
       --state-cache-mb "$STATE_MB_PER_SHARD"
       --compat-mem-budget-mb "$COMPAT_MB_PER_SHARD"
       --memory-limit-mb "$MEM_LIMIT_MB_PER_SHARD"
       --memory-policy shrink
       --threads "$THREADS_PER_SHARD"
       --parallel-depth 1
       --scheduler static
       --task-target-per-thread 64
       --split-max-depth 4
       --split-min-candidates 16
       --compat-cache-policy lru
       --force-propagation
       --async-output
       --flush-every 1024
       --jsonl
       --out "$OUT_FILE")

  if command -v numactl >/dev/null 2>&1; then
    NODE=$(( i % 2 ))
    echo "Launching shard $i on NUMA node hint $NODE"
    (numactl --cpunodebind="$NODE" --membind="$NODE" "${CMD[@]}" >"$LOG_FILE" 2>&1 || "${CMD[@]}" >"$LOG_FILE" 2>&1) &
  else
    echo "Launching shard $i"
    "${CMD[@]}" >"$LOG_FILE" 2>&1 &
  fi
done

wait

echo "Merging JSONL outputs by canonical_key..."
python3 scripts/merge_jsonl_by_key.py "$OUT_DIR/n8_prefix_merged.jsonl" "$OUT_DIR"/n8_prefix_shard_*.jsonl
echo "Merged output: $OUT_DIR/n8_prefix_merged.jsonl"
