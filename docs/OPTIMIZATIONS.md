# Implemented optimizations

The project now has two exact generation modes:

```text
--generation-mode state-cache   production default
--generation-mode canonical     low-memory / validation mode
```

The production mode is the fast quotient DFS with bounded or full exact partial-state
caching.  Canonical augmentation remains implemented, but it is no longer the default
because it replaces memory pressure by repeated canonical-parent tests and was much
slower on `n=7`.

## Current production optimizations

1. **Root-orbit symmetry breaking.**  At the empty state, one labeled image per orbit
   representative is enough.
2. **Exact partial-state canonical keys.**  States are keyed by sorted cell vertex masks
   under the selected canonicalization mode.
3. **Sharded packed state cache.**  Partial-state keys are stored in many shards instead of
   one global `unordered_set` mutex.
4. **Bounded state-cache mode.**  Eviction causes repeated work only; it cannot undercount.
5. **Temporary-free uncovered vertex selection.**  Uses `count_intersection` rather than
   allocating dense temporary bitsets.
6. **Volume filtering before branching.**
7. **Exact bounded volume-subset feasibility pruning.**
8. **Volume-filtered coverage feasibility.**
9. **Forced-cell propagation.**  A uniquely coverable uncovered vertex forces its unique
   candidate cell.
10. **Lazy compatibility rows.**  Rows are built on demand using two-hit filtering.
11. **`CompatCache::intersect_into`.**  Compatibility is applied directly to the next
    allowed set.
12. **Sparse-incompatible row representation.**  Rows can be stored as incompatible-index
    lists when this is smaller than a dense bitset.
13. **Compressed compatibility disk rows.**  Disk rows use the same dense/sparse choice.
14. **Explicit adaptive prefix-task generation.**  Parallel tasks can be refined until a target
    number of tasks is reached or a maximum prefix depth is reached, but `--auto` now
    keeps `--prefix-task-target 0` to avoid a long single-thread prepass.
15. **Prefix-task process sharding.**  Prefix JSONL files can be split among processes and
    merged by final canonical key.
16. **Per-thread DFS workspaces.**  Dense bitset buffers and ordered candidate vectors are
    reused by recursion depth.
17. **Per-thread state-counter batching.**  Progress counters do not require a hot atomic
    operation at every DFS node.
18. **Optional deep relaxed coverage-volume DP.**
19. **Explicit asynchronous output writer.**  Available with `--async-output`; auto mode keeps synchronous flush-every-solution output for visible progress.
20. **RSS memory guard and cache shrinking.**

## Exactness notes

All pruning rules are necessary-condition rules:

* A volume-too-large candidate cannot occur in a completion.
* If the remaining volume is not achievable from allowed cell volumes, no completion exists.
* If an uncovered hypersimplex vertex cannot be covered by any allowed cell, no completion exists.
* If exactly one allowed cell covers an uncovered vertex, every completion must include it.
* The deep coverage-volume DP ignores future compatibility, so impossibility in the relaxed
  problem implies impossibility in the true problem.

The state cache is not an acceptance criterion.  It only skips duplicate partial states.
Bounded-cache eviction can only reintroduce work.

Final tilings are accepted only after:

```text
volume_sum == total_volume
covered_vertex_mask == all_vertices
pairwise compatibility was enforced during construction
final canonical tiling key is new
```

## What to profile

For `n=8`, useful ratios are:

```text
duplicate_states / states
forced_cells / states
compat_misses / compat_hits
bitset_and_mb / states
state_cache_est_mb / state_cache_size
rss_mb / managed-memory estimate
```

The uploaded run data had very few compatibility misses but many duplicate states.  That is
why the current optimization emphasis is on state-cache layout, forced propagation, and
per-state memory traffic rather than on precomputing more compatibility rows.

## Auto-mode hotfix

The fastest production path is the state-cache DFS with a shallow root-orbit split.  Auto
mode therefore no longer turns on adaptive prefix generation or asynchronous output:

```text
--prefix-task-target 0
--no-async-output
--flush-every 1
```

This prevents two misleading behaviors observed on a 192-logical-CPU machine:

1. a long single-thread prefix-construction phase before workers started;
2. redirected stdout appearing frozen after exactly 64 tilings because the async writer was
   batching flushes.

Adaptive prefixing and async output remain available explicitly.
