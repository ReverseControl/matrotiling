# Packed state cache and auto feedback pilot

This document describes the latest production optimizations.

## 1. Packed exact state cache

The production search is the fast exact state-cache quotient DFS.  Its main repeated operation is:

```text
canonical partial state key -> lookup/insert in visited partial-state cache
```

Earlier versions stored these keys as `std::string` objects inside sharded `std::unordered_set`
containers.  That was correct but expensive: each key carried allocator overhead, bucket pointers,
and poor locality.

The new backend is still exact but stores keys compactly:

```text
per shard:
  open-addressing slot table
  entry array: hash, byte offset, byte length, alive flag
  byte arena containing raw canonical-key bytes
  FIFO queue for bounded eviction
```

The hash is never used as a proof of equality.  It only selects a probe sequence.  A hit is accepted
only after comparing:

```text
hash
length
all key bytes
```

Thus the bounded packed cache cannot introduce false positives and cannot undercount.

## 2. Byte-budget enforcement

`--state-cache-mb MB` now sets a real byte budget, not only an approximate entry count.  The code
still computes an entry-count estimate for the memory plan, but bounded eviction checks both:

```text
live entry count <= state_cache_max
live packed bytes <= state_cache_mb
```

When evicted payload bytes accumulate, each shard compacts its arena and rebuilds its table.  This
prevents slow growth from dead payloads.

Progress lines now include:

```text
state_cache_live_mb
state_cache_alloc_mb
```

`state_cache_live_mb` is the live packed cache footprint.  `state_cache_alloc_mb` includes allocated
arena/table capacity and is the better number for diagnosing RSS.

## 3. Optional feedback pilot

`--auto-pilot-states N` runs a short exact pilot search before the real run.  It measures:

```text
duplicate ratio = duplicate_states / (states + duplicate_states)
compatibility miss rate = compat_misses / (compat_hits + compat_misses)
peak active workers / requested threads
coverage and deep-DP prune counts
```

Then it adjusts safe parameters:

```text
high duplicate ratio       -> increase state-cache budget if not explicitly set
tiny compat miss rate      -> use direct compatibility cache and reduce compat RAM if not explicit
low worker utilization     -> increase task target, increase split depth, lower split-min-candidates
zero deep coverage pruning -> make deep DP more conservative
```

The pilot does not change the enumeration target.  It only chooses resource parameters.  It is useful
for new systems because the best split/cache settings vary by CPU count, memory bandwidth, NUMA
layout, and available RAM.

Example:

```bash
./build/alexeev_r3_cpp 8 --auto --auto-pilot-states 1000000 --out r3n8_tilings.txt
```

## 4. Exactness notes

The packed state cache and pilot preserve correctness.

Evicting a partial-state key can cause repeated work, but it cannot skip a valid tiling.  The final
acceptance criteria are unchanged:

```text
pairwise face fitting
full hypersimplex vertex coverage
volume sum equal to total volume
exact quotient deduplication of final tiling keys
```

The pilot run is discarded after parameter measurement.  It does not contribute solutions to the
main output.

## 5. When to use the pilot

Use plain `--auto` for routine runs on already-characterized systems.

Use a pilot when:

```text
running on a new CPU/memory configuration
CPU utilization looks too low
the compatibility miss rate or duplicate ratio differs from known logs
testing NUMA/process-sharded workflows
```

Recommended pilot sizes:

```text
small test:       --auto-pilot-states 100000
serious tuning:   --auto-pilot-states 1000000
large pilot:      --auto-pilot-states 5000000
```

The pilot adds overhead, so do not use a very large pilot unless the full run is expected to last long
enough that retuning pays for itself.


## Regression-fix note

As of the regression-fixed package, `--auto` uses the measured-fast static scheduler and LRU compatibility cache by default.  Work stealing, direct compatibility caching, packed-cache experiments, and auto pilot feedback remain available explicitly, but they are no longer production defaults because user timing showed slower time-to-solution on the 192-logical-CPU `n=8` run.
