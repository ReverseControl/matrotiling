# Performance feedback v4 — 2026-04-27

## Data used

The new warm-profiled `perf_data.txt` was an exact but truncated benchmark:

```text
n=8
images=788763
target=10000 solutions
matrix=full
cache_mode=warm-profiled
```

The strongest rows are now centered on:

```text
threads=160
scheduler=static-hybrid
compat-cache-policy=lru
worker-compat-cache-size=64
prewarm-hot-compat-limit=512
affinity=none
parallel-depth=1
```

The best row in the newest data is approximately:

```text
threads_160_static_hybrid_lru_worker_cache_64
elapsed_sec        about 63 sec
solutions/sec      about 159
effective cores    about 117
```

A close second is the analogous direct-compatibility row with worker cache and
hot-row prewarm.  The data also confirms that `parallel-depth=2`, full
`work-steal`, compact/spread affinity, thread-local state-cache scope, and
labeled-state prefilter remain anti-patterns for time-to-10K tilings on the tested
system.

## Code changes made

### 1. Throughput auto profile

`--auto --auto-profile throughput` now remains locked to the best measured
production backbone:

```text
threads                  about 5/3 physical cores, capped by logical CPUs minus 32
scheduler                static-hybrid
parallel-depth           1
compat-cache-policy      lru
worker-compat-cache-size 64
affinity                 none
state-cache-scope        global
bitset-kernel            auto
state-key-format         binary
```

On a 96c/192t dual Threadripper, this gives 160 worker threads.

### 2. Automatic hot-row recording

When `--auto` is used for `n>=8`, the program now automatically records hot
compatibility rows to:

```text
<cache-dir>/hot_compat_rows_n8.txt
```

This creates a profile for subsequent runs.  If a hot-row profile already exists,
`--auto` prewarms the top 512 rows.  Missing files are ignored.

### 3. Gather-data matrix update

`--gather-data --gather-matrix full` now includes focused v4 experiments:

```text
v4_best_threads_<T>_hybrid_lru_worker64_prewarm512
v4_near_best_threads_<T>_hybrid_direct_worker64_prewarm512
v4_threads_<T>_hybrid_lru_worker64_prewarm2048
v4_threads_<T>_hybrid_lru_worker128_prewarm512
v4_best_repeat_end_threads_<T>_hybrid_lru_worker64_prewarm512
```

It also expands thread and donation sweeps around the observed optimum:

```text
threads: 144, 152, 160, 168, 176, plus earlier reference values
donation thresholds: 0.60, 0.70, 0.80, 0.85, 0.90
split_min_candidates: 8, 12, 16, 24, 32, 64
split_max_depth: 3, 4, 5
```

### 4. Learned profile support

`--auto-profile learned --auto-learn perf_data.txt` continues to select the best
safe row from a local benchmark file.  It is allowed to learn worker compatibility
cache size, hot-row prewarm limit/path, compatibility policy, thread count, and
scheduler, while rejecting known anti-patterns.

## How to profile another machine

Use this first:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix full \
  --gather-cache-mode warm-profiled \
  --gather-warm-solutions 10000 \
  --gather-solutions 10000
```

Then summarize:

```bash
python3 scripts/summarize_perf_data.py perf_data.txt
```

For smaller machines, use:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix core \
  --gather-cache-mode warm-profiled \
  --gather-warm-solutions 3000 \
  --gather-solutions 3000
```

## Interpreting the output

Use solution benchmarks for streaming time-to-output:

```text
solutions/sec
elapsed_sec for 10000 solutions
```

Use state benchmarks for raw search throughput:

```text
states/sec
duplicate ratio
effective cores
```

A profile is not automatically good because it uses many cores.  Full work stealing
used many cores in prior tests, but explored a less solution-dense part of the tree
and was much slower to emit 10,000 tilings.  Conversely, static-hybrid late donation
keeps the productive static traversal while redistributing long-tail work.

## Current next optimization targets

The remaining safe optimization opportunities are:

```text
1. long-run profiling beyond the first 10K tilings,
2. NUMA process sharding using the now-fast static-hybrid profile,
3. PGO/LTO/native builds,
4. root-performance ordering learned from full prefix/task runs,
5. detailed lock-wait timing for state-cache shards.
```

Avoid lossy duplicate filters, unproved dominance pruning, or changing the target
from abstract matroid tilings to realizable/oriented matroids.
