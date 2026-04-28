# Perf feedback v3 tuning, 2026-04-27

This document records the latest update to the `--auto` and `--gather-data` policies after
the warm-profiled `perf_data.txt` run on a 192-logical / 96-physical-core dual Threadripper
system.

The mathematical search is unchanged.  The program still enumerates exact Alexeev-style
rank-3 matroid tilings of `Delta(3,n)`, using the included `allr3n*.txt` orbit-reference
files.  These changes only affect traversal order, cache use, worker-local compatibility
lookups, and benchmark coverage.

## What the data showed

The newest warm-profiled run showed the following high-value rows for `n=8`, truncated at
10,000 solutions:

```text
threads_160_static_hybrid_lru_worker_cache_64
  elapsed_sec       63.576
  solutions/sec     157.292
  scheduler         static-hybrid
  compat policy     lru
  worker cache      64
  affinity          none

threads_160_static_hybrid_lru_hot_prewarm_512
  elapsed_sec       67.314
  solutions/sec     148.556

throughput_static_hybrid_lru
  elapsed_sec       68.589
  solutions/sec     145.797

balanced_static_hybrid_lru_repeat_end
  elapsed_sec       69.625
  solutions/sec     143.627

threads_96_static_hybrid_lru_affinity_numa
  elapsed_sec       78.153
  solutions/sec     about 128

threads_96_static_hybrid_lru
  elapsed_sec       84.500
  solutions/sec     118.343
```

The interpretation is:

1. `static-hybrid` remains the best scheduler family.
2. `parallel-depth=1` remains mandatory for production.
3. A worker-local compatibility row cache of size 64 is now promoted only for the
   throughput profile, where it produced the best observed result.
4. NUMA affinity remains useful for the interactive 96-thread profile, but it hurt the
   160-thread throughput profile.
5. Hot compatibility row prewarm is useful when the hot-row file already exists, but the
   program should not blindly prewarm arbitrary rows.

## Updated auto profiles

On a 96c/192t host, `--auto` now selects approximately:

```text
throughput:
  threads=160
  scheduler=static-hybrid
  compat-cache-policy=lru
  worker-compat-cache-size=64
  affinity=none
  hot-row prewarm=512 if a hot-row profile file exists

balanced:
  threads=144
  scheduler=static-hybrid
  compat-cache-policy=lru
  worker-compat-cache-size=0
  affinity=none
  hot-row prewarm=512 if a hot-row profile file exists

interactive:
  threads=96
  scheduler=static-hybrid
  compat-cache-policy=lru
  worker-compat-cache-size=0
  affinity=numa when multiple NUMA nodes are detected
  hot-row prewarm=512 if a hot-row profile file exists
```

## Hot-row profile discovery

Auto mode looks for hot compatibility rows in these locations:

```text
<cache-dir>/hot_compat_rows_n8.txt
<cache-dir>/perf_hot_compat_rows_n8.txt
<cache-dir>/perf_hot_compat_rows.txt
<cache-dir>/hot_compat_rows.txt
./hot_compat_rows_n8.txt
./perf_hot_compat_rows_n8.txt
./perf_hot_compat_rows.txt
./hot_compat_rows.txt
```

If no file exists, auto mode does not prewarm and does not delay time-to-first-solution.

To produce a hot-row file, run:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix full \
  --gather-cache-mode warm-profiled \
  --gather-warm-solutions 10000 --gather-solutions 10000
```

The gather warmup writes `perf_hot_compat_rows.txt` in the current directory.  You may
copy it into `.alexeev_cache/` for auto reuse:

```bash
cp perf_hot_compat_rows.txt .alexeev_cache/perf_hot_compat_rows.txt
```

## Learned profile changes

`--auto-profile learned --auto-learn perf_data.txt` now reads and applies:

```text
worker_compat_cache_size
prewarm_hot_compat_limit
prewarm_hot_compat_rows, if that path exists
```

If the stored `prewarm_hot_compat_rows` path is stale, learned mode falls back to the
standard hot-row discovery locations.

## Gather-data additions

The full gather matrix now includes explicit v3 retests:

```text
throughput_best_v3_static_hybrid_lru_worker_cache64_prewarm512
throughput_worker_cache64_no_hot_prewarm
throughput_lru_notouch_worker_cache64_prewarm512
throughput_direct_worker_cache64_prewarm512
```

These are intended to distinguish three effects:

1. worker-local compatibility row cache,
2. hot compatibility row prewarm,
3. LRU vs LRU-notouch vs direct compatibility policy.

## Anti-patterns still not promoted

The following remain benchmark references only:

```text
full work-steal
parallel-depth 2
compact affinity
spread affinity
thread-local state cache
labeled-state prefilter
forced unrolled/AVX bitset kernels
canonical augmentation as production mode
```

They are mathematically safe where implemented, but the observed time-to-10K behavior
was worse than the static/static-hybrid state-cache production path.

## Recommended commands

Throughput:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile throughput \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

Balanced:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile balanced \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

Interactive:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile interactive \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

Next feedback run:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix full \
  --gather-cache-mode warm-profiled \
  --gather-warm-solutions 10000 --gather-solutions 10000
```

Then:

```bash
python3 scripts/summarize_perf_data.py perf_data.txt
```
