# Gathering performance data for Delta(3,9)

The `n=9` path is a separate wide-mask engine.  It exists because
\(\Delta(3,9)\) has `C(9,3)=84` vertices, so a matroid cell vertex set no longer
fits in a single `uint64_t`.  The engine uses `Mask128` two-limb storage and
keeps the optimized `n<=8` path untouched.

## Required input

Place a validated rank-3 orbit file at:

```text
data/allr3n9.txt
```

with the same JSONL format as the `allr3n8.txt` reference files.

The uniform orbit should have:

```json
{"ineqs": [], "n": 9, "orbit_id": 0, "volume": 36, "volume_sha": 36, "volume_lattice": ...}
```

## First profiling run

Use a state-bounded run first.  This avoids waiting for an unknown number of
early tilings before getting useful scheduler/cache data.

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix core \
  --gather-states 100000 --gather-solutions 0
```

## Broader profiling run

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix full \
  --gather-states 100000 --gather-solutions 0
```

## Solution-bounded profiling

Once the first data shows that solutions appear quickly:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix core \
  --gather-solutions 1000
```

## What the n=9 records mean

Every record includes:

```text
engine                       n9-wide
vertices                     84
orbits                       number of orbit representatives in allr3n9.txt
images                       distinct labeled images generated under S_9
threads                      worker threads used for root-static search
compat_row_capacity          dense-row-equivalent compatibility rows
state_cache_max              bounded labeled-state cache capacity
quotient_final_dedup         whether final brute S_9 quotient deduplication is enabled
```

The current `n=9` gather matrix tests thread count, state-cache budget,
compatibility-cache budget, forced propagation, and a no-state-cache reference mode.
The previous `n9_labeled_no_final_quotient_reference` row has been removed from
all default gather matrices because on real `n=9` inputs it can run for hours and
is not useful for fast feedback tuning.  Labeled `n=9` runs are still available by
explicit CLI options, but they are no longer part of `--gather-data`.  The matrix
does **not** claim that the tuned `n=8`
static-hybrid scheduler is optimal for `n=9`; collecting these records is the
first step toward designing the correct `n=9` auto profile.

## Slow reference removed from the default matrix

On a 96-physical-core / 192-logical-thread machine, the old full matrix had 33
experiments.  After this change it has 32 experiments: the only removed row is
the slow labeled/no-final-quotient reference that formerly appeared after the
first 18 probes.


## 2026-04-28 Threadripper feedback update

The first full n=9 gather-data file on the 96-core / 192-thread dual
Threadripper system used the wide engine with:

```text
orbits         901
labeled images 70,731,920
max_states     10,000
```

The best first-pass thread count was `128`, not `144`, `160`, or `192`.
Accordingly, the n=9 auto profile now uses:

```text
throughput/balanced: 128 threads
interactive:          96 threads
memory-saver:         48 threads
```

The n=9 gather matrix was refocused around:

```text
96, 112, 120, 128, 136, 144, 152, 160
```

with 176/192 retained only as full-matrix regression sentinels.

The most useful next run is:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix core \
  --gather-states 100000 --gather-solutions 0
```

If that completes comfortably, run the full matrix:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix full \
  --gather-states 100000 --gather-solutions 0
```
