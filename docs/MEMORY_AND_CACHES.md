# Memory and cache controls

The program prints a memory/resource plan before DFS.  The important quantities are:

```text
N = number of labeled cell images
row_bytes = 8 * ceil(N/64)
```

For `n=8`, `N` is expected to be 788,763, so a dense row is about 98,600 bytes.

## Compatibility cache

`--compat-mem-budget-mb MB` is converted to a row count:

```text
rows = floor(MB * 2^20 / row_bytes)
```

Compatibility rows are stored in the LRU either as dense compatible bitsets or sparse
incompatible-index lists, whichever is smaller.  The `compat_lru_payload_mb` counter reports
the live payload, which may be less than the dense-row equivalent.

## State cache

`--state-cache-mb MB` is converted to an exact entry cap using a conservative bytes-per-entry
estimate.  Because `std::unordered_set` and `std::string` allocator overhead are
implementation-dependent, this is an estimate rather than a formal allocator-level cap.  The
RSS memory guard is the hard runtime backstop.

Use:

```bash
--memory-limit-mb <MB> --memory-policy shrink
```

for long `n=8` runs.

## Memory policy

```text
warn    print a warning if RSS exceeds the limit
shrink  halve the bounded state cache, evict to the new cap, clear compatibility and canonical LRU caches
abort   terminate the process
```

## Solution dedup

`--solution-dedup memory` streams output and keeps final canonical keys in memory.

`--solution-dedup disk` writes `(canonical_key, chosen_indices)` records to a spool file,
then sorts/deduplicates after the search.  This delays output but avoids a growing final
solution-key table during the DFS.


## Streaming versus disk-spooled solution deduplication

`--solution-dedup memory` stores final canonical solution keys in RAM and writes/flushed accepted tilings immediately.  This is now the auto-mode default because the large memory-growth failure came from the partial-state cache, not from final solution keys.

`--solution-dedup disk` writes `(canonical_key, chosen_cells)` records to a spool file and performs a sort/unique merge at the end.  It is exact, but the user-facing output file remains empty until the run finishes.  In this mode, progress lines report `solution_candidates` so users can see that complete tilings have been found even before final deduplication.


## Compressed disk compatibility rows

The compatibility disk cache now stores rows in a tagged binary format:

```text
magic/version
nbits
kind = dense compatible bitset | sparse incompatible list
payload count
payload
```

Older dense `DynBitset` row files are still accepted.  New files may be much smaller when
the incompatible set is sparse.

## Output memory

`--async-output` stores formatted solution records in a short queue consumed by a writer
thread.  The queue is normally tiny because the writer drains it continuously.  If output is
redirected to a slow filesystem and the queue grows, reduce `--threads`, increase
`--flush-every`, or use `--solution-dedup disk`.

## Prefix task files

Prefix task files store image-index prefixes, not dense allowed bitsets.  When a prefix file is
read, the program reconstructs coverage, volume, and allowed sets by replaying the prefix
cells and intersecting compatibility rows.  This keeps prefix files small and robust across
machines.


## Packed state-cache memory accounting

The bounded state cache now has a packed backend.  In progress and final summaries:

```text
state_cache_live_mb
```

is the live exact key data plus table/entry/FIFO overhead currently needed for active cache entries.

```text
state_cache_alloc_mb
```

is allocated capacity, including slack in arenas and open-addressing tables.

The RSS of the process may be larger because it also includes images, compatibility rows, canonical
memo tables, thread stacks, filesystem page cache, allocator metadata, and executable/library pages.

The memory guard uses RSS, while `--state-cache-mb` controls the packed cache's own byte budget.
If the RSS guard triggers with `--memory-policy shrink`, the state cache entry and byte caps are
halved and optional compatibility/canonicalization caches are cleared.

