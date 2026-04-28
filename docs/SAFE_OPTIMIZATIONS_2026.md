# Production optimizations: implementation notes

This document records the ten safe optimizations added after the first long `n=8`
production run showed the characteristic state-cache profile:

```text
states                    ~ 10,000,000
solutions                 ~ 43,309
duplicate_states          ~ 29,713,088
compat_misses             ~ 6,520
state_cache_size          ~ 10,000,001
rss_mb                    ~ 5,335
```

The important conclusion is that the run was not dominated by compatibility misses.
It was dominated by the number of partial states, duplicate paths, and per-state overhead.
The changes below therefore optimize the state-cache search path rather than returning
to canonical augmentation.

## Hotfix: auto no longer serializes prefix generation

A later large-system test showed that automatic adaptive prefix generation was too
aggressive.  With `--prefix-task-target` set automatically, the program could spend a long
time in the single-thread prefix-construction pass before launching the DFS workers.  In
`htop`, this looked like one busy thread and no progress.  The fix is:

```text
auto mode:          prefix_task_target = 0
explicit prefixing: --prefix-task-target K
```

The shallow root-orbit split now starts workers immediately.  Additionally, prefix frontier
states are emitted before forced-cell propagation; the worker applies forced propagation
when it consumes the task.  This preserves correctness and avoids serializing exact pruning
that belongs in the parallel DFS.

A separate output regression was also fixed: auto mode no longer enables async output
with `flush_every=64`, because redirected stdout could appear stuck after exactly 64 tilings.
Use `--async-output --flush-every K` explicitly when batching output is desired.

## 1. Temporary-free uncovered-vertex selection

Old pattern:

```cpp
DynBitset cand = allowed.bit_and(by_vertex[v]);
count = cand.count();
```

New pattern:

```cpp
count = allowed.count_intersection(by_vertex[v]);
```

For `n=8`, one dense image-index row is about 98,600 bytes.  Avoiding a temporary row
for each uncovered vertex saves allocation pressure and memory bandwidth at every state.

## 2. `CompatCache::intersect_into`

The DFS previously materialized a compatibility row and then intersected it:

```cpp
DynBitset comp = compat.get(i);
new_allowed = allowed;
new_allowed &= comp;
```

The cache now has:

```cpp
compat.intersect_into(i, allowed, new_allowed);
```

This is important because compatibility rows may be stored as sparse incompatible-index
lists.  In that case the operation is:

```cpp
new_allowed = allowed;
for (j : incompatible[i]) new_allowed.reset(j);
```

No dense row materialization is required.

## 3. Sharded packed exact state cache

The old cache was a single mutex-protected `unordered_set<string>`.  The new cache is:

```text
ShardedStateCache
  shard 0: mutex + unordered_set<binary_key> + FIFO
  shard 1: mutex + unordered_set<binary_key> + FIFO
  ...
```

Keys are exact binary canonical keys, not lossy hashes.  The hash chooses a shard; equality
still compares the stored key.  False-positive pruning is never allowed.

Bounded mode evicts per shard.  This is exact because eviction means "forget this partial
state"; forgetting can only cause repeated work.

## 4. Per-thread state-counter batching

DFS state visits are counted in thread-local batches and flushed periodically to the global
atomic counter.  This avoids a hot atomic increment at every node.  Progress output remains
batched; final enumeration correctness does not depend on the counter.

## 5. Forced-cell propagation

At a state, after volume filtering, for each uncovered hypersimplex vertex `v` compute:

```cpp
count(allowedR & by_vertex[v])
```

If the count is zero, the state is impossible.  If the count is one, the unique cell must occur
in every completion, so it is inserted immediately.

This is exact because every complete tiling must cover every hypersimplex vertex.

Command-line control:

```bash
--force-propagation
--no-force-propagation
```

It is enabled by default in state-cache production mode.

## 6. Adaptive prefix-task generation

The program supports adaptive prefix generation:

```bash
--parallel-depth <minimum_depth>
--prefix-max-depth <maximum_depth>
--prefix-task-target <number_of_tasks>
```

The collector can refine shallow states into a larger set of prefix tasks.  However, this is
now explicit only.  Auto mode keeps:

```text
--prefix-task-target 0
```

so it starts the worker pool immediately after the root-orbit split.  On `n=8`, the normal
auto run should quickly report about 186 initial tasks.  Use adaptive prefixing mainly for
`--write-prefix-tasks` and NUMA/process-sharded runs.

## 7. Prefix-task multi-process sharding

A prefix task file can be generated and then split among processes.

Generate tasks:

```bash
./build/alexeev_r3_cpp 8 --auto \
  --prefix-task-target 20000 --prefix-max-depth 2 \
  --write-prefix-tasks n8_prefix_tasks.jsonl
```

Search one shard:

```bash
./build/alexeev_r3_cpp 8 --read-prefix-tasks n8_prefix_tasks.jsonl \
  --prefix-shard 3/16 --threads 12 --jsonl \
  --out shard_03.jsonl
```

Merge:

```bash
python3 scripts/merge_jsonl_by_key.py merged.jsonl shard_*.jsonl
```

The merge step is exact because final records carry canonical keys.

## 8. Asynchronous output writer

With many worker threads, synchronous solution writing can become a lock bottleneck.
Writer-thread mode is still available explicitly:

```bash
--async-output --flush-every 64
```

Production auto mode no longer enables it automatically.  The default is immediate
write-and-flush, which is more useful for `./alexeev_r3_cpp 8 --auto > r3n8_solutions.txt`
and for short `--max` tests.  Use async output only when output locking is measured to be a
bottleneck.

## 9. Compressed compatibility disk rows

Disk row files now store a representation tag:

```text
dense compatible bitset
or
sparse incompatible index list
```

The loader remains backward compatible with the previous dense `DynBitset` row format.

## 10. Faster adaptive deep coverage-volume DP

`--deep-cover-dp` is still optional.  The DP is now multiplicity-compressed by candidate
footprint and volume, and volume reachability is packed into a 32-bit word because
`sha` remaining volume is at most 25 for `n=8`.

The DP is a relaxed covering problem: it ignores future pairwise compatibility, so a failure
is a rigorous prune, while success is only a necessary condition.

## Recommended next profiling counters

Watch these first:

```text
duplicate_states / states
forced_cells
compat_intersect_ops
compat_rows_sparse_incompat
prefix_tasks
bitset_and_mb / states
rss_mb / state_cache_size
```

A high `duplicate_states / states` ratio means the bounded state cache is still doing useful
work.  If RSS is low, increase `--state-cache-mb`.  If RSS approaches the memory guard,
decrease `--state-cache-mb` or increase process sharding.
