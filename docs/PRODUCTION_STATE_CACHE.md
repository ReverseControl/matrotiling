# Production state-cache search

The production search is the fast quotient DFS with a partial-state cache.

At a state `T`, the algorithm stores an exact canonical key for the sorted tuple of cell
vertex masks.  If another DFS path reaches the same key, it is skipped.

This is exact because the state cache is only a duplicate-work pruner.  The mathematical
acceptance test for a tiling remains:

```text
pairwise face-fitting enforced during construction
volume_sum == total volume
covered_vertex_mask == all vertices
final canonical tiling key not seen before
```

## Bounded cache exactness

In bounded mode, old partial-state keys are evicted.

Eviction can only cause repeated work.  It cannot cause undercounting because an evicted
key is not treated as present.  Completed tilings are still deduplicated by final canonical
keys.

## Why this is faster than canonical augmentation

Canonical augmentation avoids a global partial-state cache but pays for repeated
canonical-parent tests.  For `n=7`, that was significantly slower.  The production path
restores the safe uncovered-vertex branching rule:

```text
choose an uncovered hypersimplex vertex v with fewest volume-feasible candidates;
branch only on compatible candidates covering v.
```

This rule is exact for ordinary DFS: every completion of the current state must eventually
cover `v`.

## State-cache modes

```text
full      fastest when memory is sufficient; recommended for n=7
bounded   recommended for n=8
none      exact but usually much slower
auto      full for n<=7, bounded for n=8
```


## Sharded implementation

The cache is now a `ShardedStateCache`.  Each shard has its own mutex, exact key table,
and FIFO eviction queue.  A hash chooses the shard, but equality is still exact on the full
binary canonical key.

This reduces contention relative to a single global visited-state table.  It also makes bounded
eviction cheaper: each shard evicts only from its own queue.

## Forced propagation in state-cache mode

Before ordinary branching, the DFS checks uncovered hypersimplex vertices.  If some
uncovered vertex has exactly one remaining volume-feasible compatible cell, that cell is
forced.  The forced cell is inserted into the exact state cache and the compatibility/coverage
state is updated.  Propagation repeats until no forced cell remains.

This is disabled in canonical-augmentation mode because canonical augmentation has a
different construction-path discipline.

## Adaptive parallel prefixes

`--parallel-depth` is the minimum split depth.  `--prefix-task-target` and
`--prefix-max-depth` allow the collector to continue refining shallow prefixes until enough
tasks exist for a large worker pool.

Use `--prefix-task-target 0` to recover fixed-depth behavior.
