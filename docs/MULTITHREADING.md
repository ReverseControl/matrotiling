# Multithreading

The program uses standard C++ threads, atomics, mutexes, and condition variables.  It does
not require OpenMP.

Parallelized phases:

```text
labeled image generation
compatibility prewarming
parallel DFS after task splitting
```

`--threads 1` is deterministic serial mode.
`--threads 0` uses all logical CPUs.
`--auto` chooses a more conservative physical-core-scale thread count.

## Parallel DFS

`--parallel-depth D` expands the root search to depth `D`, creates prefix tasks, and
worker threads consume those tasks from an atomic counter.

For production state-cache search, the shared structures are:

```text
visited_states          mutex-protected exact partial-state keys
compatibility LRU       mutex/condition-variable protected lazy rows
canonical memo          mutex-protected bounded memo
solution dedup          memory set or disk spool
output stream           mutex-protected
```

For high-core NUMA machines, prefer several shard processes over one huge thread pool.
See `scripts/run_n8_shards_example.sh`.

## Thread count guidance

Dense bitset operations and cache traffic can saturate memory bandwidth.  More hardware
threads are not always faster.

Recommended starts:

```text
32 CPU / 64 GB:       --threads 24
192 logical / 512 GB: --threads 96
```

Then benchmark `states/sec`, `compat_misses`, RSS, and elapsed time.

## Auto-mode worker-start policy

`--auto` now uses the shallow root-orbit split:

```text
--parallel-depth 1
--prefix-task-target 0
```

This means the program should quickly print approximately:

```text
parallel search: 186 initial tasks ... using <threads> threads
```

for `n=8`, then the worker pool is active.  Adaptive prefix generation is explicit because
building thousands of prefix states is itself a DFS prepass and can be single-threaded for a
long time.

The task frontier is emitted before forced-cell propagation.  The worker applies forced
propagation when it consumes the task.  This keeps exactness but moves expensive pruning
into the parallel region.
