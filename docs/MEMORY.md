# Memory model

The previous unbounded-memory failure mode was the global partial-state table.  Production
mode now uses the fast state-cache search with explicit memory controls.

## Fixed memory

```text
image metadata
by_vertex membership bitsets
permutation tables
orbit and inequality data
```

## Bounded/growing memory

```text
compatibility LRU
state-cache visited_states
canonicalization memo
solution dedup keys or disk spool
thread workspaces
```

## State cache

`--state-cache full` is fastest but unbounded.  Recommended for `n=7`.

`--state-cache bounded --state-cache-mb MB` is recommended for `n=8`.  Eviction causes
duplicate work but cannot undercount because final solution keys are exact.

## Memory plan

Before DFS the program prints:

```text
detected CPU/memory/cache resources
dense row size
compatibility LRU budget
state-cache entry cap and estimated MB
canonicalization LRU estimate
thread workspace estimate
RSS guard
```

The printed managed-memory cap is an estimate.  STL allocator overhead and OS page cache
are implementation-dependent.  Use the RSS guard for a runtime backstop.

## RSS guard

```bash
--memory-limit-mb 430000 --memory-policy shrink
```

In `shrink` mode the program halves the bounded state-cache cap, evicts to the new cap,
clears the compatibility LRU, and clears the canonicalization LRU.
