# v3 feedback update

The previous best row was `threads_160_static_hybrid_lru` at about 68.6 seconds for
10,000 tilings.  The new warm-profiled data found a faster row:

```text
threads_160_static_hybrid_lru_worker_cache_64
elapsed_sec   63.576
solutions/sec 157.292
```

So throughput auto now adds `--worker-compat-cache-size 64`.  It also reuses a hot
compatibility-row profile at limit 512 when such a file exists.

# 2026-04-27 perf-feedback tuning update

This update uses the latest `perf_data.txt` feedback from the dual Threadripper system.

## Empirical result used

The best measured 10K-solution profile was:

```text
threads_160_static_hybrid_lru
elapsed_sec   about 68.1
solutions/sec about 146.8
effective cores about 116
```

The closest safe production cluster was:

```text
160 static-hybrid lru / lru-notouch / direct
144 static-hybrid lru
128 static-hybrid direct/lru
```

The persistent regressions were:

```text
full work-steal
static parallel-depth 2
compact affinity
spread affinity
thread-local state cache
labeled-state prefilter
forced unrolled/AVX bitset kernels
```

## Code changes

`--auto` now chooses:

```text
throughput  -> 160-thread class static-hybrid + lru on 96c/192t systems
balanced    -> 144-thread class static-hybrid + lru
interactive -> 96-thread class static-hybrid + lru + numa affinity if available
```

`--auto-profile learned --auto-learn perf_data.txt` now rejects additional empirical anti-patterns:

```text
compact/spread affinity
unrolled/avx2/avx512 bitset kernels
non-global state cache
work-steal
parallel-depth > 1
labeled-state prefilter
```

`--gather-data` now includes high-priority combination probes:

```text
160/144/96 static-hybrid + lru
+ numa affinity
+ worker compatibility cache size 64
+ both numa and worker cache
128 static-hybrid direct + worker cache 64
hot compatibility row prewarm 512 and 2048
repeat/reference sentinel rows
```

## Why this is safe

All changes are parameter choices or representation/cache choices. They do not change the tiling definition, compatibility test, volume condition, coverage condition, or final quotient deduplication.

## Next feedback loop

Run:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix full \
  --gather-cache-mode warm-profiled \
  --gather-warm-solutions 10000 --gather-solutions 10000
```

Then summarize:

```bash
python3 scripts/summarize_perf_data.py perf_data.txt
```
