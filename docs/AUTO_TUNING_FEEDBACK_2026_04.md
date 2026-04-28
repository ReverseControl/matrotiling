# Auto tuning feedback update, 2026-04

This update promotes only changes supported by the supplied dual Threadripper
`perf_data.txt`.  The guiding rule is: keep the measured-fast static production path, and
add only narrow changes around it.

## Observed facts from the supplied data

The best time-to-10K records were:

```text
threads_128_static_lru
static_hybrid_late_donation
threads_112_static_lru
threads_80_static_lru
```

The clearly bad defaults were:

```text
work_steal
static_lru_depth2
labeled_state_prefilter_1M
```

The direct compatibility cache and LRU-notouch variants were close to LRU, but the data
was affected by sequential cache warming.  They remain in the gather matrix, but LRU
remains the auto default until cold/warm controlled runs show otherwise.

## Auto profile changes

`--auto-profile throughput` now uses modest SMT oversubscription on systems with
simultaneous multithreading:

```text
threads = min(logical, ceil(4/3 * physical))
scheduler = static
parallel_depth = 1
compat_cache_policy = lru
duplicate_local_cache_max = 65536
```

On the reference 96-core/192-thread system this gives `threads=128`.

`--auto-profile balanced` uses:

```text
threads = physical
scheduler = static-hybrid
parallel_depth = 1
donate_when_active_below = 0.75
split_max_depth = 4
split_min_candidates = 16
compat_cache_policy = lru
duplicate_local_cache_max = 65536
```

`--auto-profile interactive` uses:

```text
threads = physical - headroom
scheduler = static
parallel_depth = 1
```

On the reference system this gives `threads=80`.

## Why full work stealing is not default

The work-stealing experiment had high effective core usage, but it reached 10K tilings much
more slowly.  That means it changed traversal toward a less solution-dense region and
created more overhead for early-output throughput.  It remains useful as an experiment and
may still matter for full enumeration, but it is not a default.

## Why static depth 2 is not default

Static `parallel-depth=2` was a severe regression in the supplied run.  It is kept in
`--gather-data` only as a reference anti-pattern so that future changes do not
accidentally reintroduce it.

## Next data requested

The most useful next files are:

```bash
./build/alexeev_r3_cpp 8 --auto --gather-data \
  --gather-matrix full --gather-cache-mode shared --gather-solutions 10000

./build/alexeev_r3_cpp 8 --auto --gather-data \
  --gather-matrix core --gather-cache-mode reset --gather-solutions 10000

./build/alexeev_r3_cpp 8 --auto --gather-data \
  --gather-matrix core --gather-cache-mode shared \
  --gather-states 1000000 --gather-solutions 0
```
