Based on Moduli of Weighted Hyperplane Arrangements by Valery Alexeev. This book can be found on amazon.


TODO: the n=8 case ha been highly optimized for dual Threadripper systems already. Here the n=9 still needs optimization work to be able to compute the full tiling of n=9. Any work on this is welcomed as PR and will be integrated if it improves the current implementation. Structural algorithmic improvements are welcomed and perf-tools based improvements are highly appreciated; currently the instruction retirement rate is very low, less than in 1 in my testing with ZEN Threadripper, and the L1 cache miss rate is also very high so we need to work on data locality.

The full tilings will be placed as releases of this repo. As that is the actual result we are looking for with this code.

# Alexeev rank-3 tiling enumerator — n=9 wide auto tuning

This project enumerates Alexeev-style rank-3 matroid-polytopal tilings of
\(\Delta(3,n)\) from the included `allr3n*.txt` orbit-reference files.  The
production search is the measured-fast exact quotient DFS:

```text
state-cache search
parallel-depth 1
global exact partial-state cache
static-hybrid late donation for high-core systems
final exact quotient deduplication
```

The current `--auto` profiles are tuned from repeated `n=8` gather-data runs on a
192-logical / 96-physical-core dual Threadripper system.  The best measured
10,000-solution profile in the newest data was:

```text
threads=160
scheduler=static-hybrid
compat-cache-policy=lru
worker-compat-cache-size=64
prewarm-hot-compat-limit=512, when a hot-row profile file exists
affinity=none
parallel-depth=1
```

It measured roughly 63 seconds for 10,000 emitted tilings on that system.  This is
a benchmark result, not a mathematical shortcut: the enumeration remains exact.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Optional high-performance build:

```bash
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release \
  -DALEXEEV_ENABLE_NATIVE=ON -DALEXEEV_ENABLE_LTO=ON
cmake --build build-native -j
```

## Validate

```bash
./build/alexeev_r3_cpp --self-test --data-dir data --count-only
```

Expected small quotient counts including the trivial tiling:

```text
n=4: 1
n=5: 3
n=6: 26
```

## Production runs

Throughput profile for the dual Threadripper-like machine:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile throughput \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

Balanced profile:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile balanced \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

Interactive profile with more desktop headroom:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile interactive \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

Machine-specific learned profile from previous data:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile learned --auto-learn perf_data.txt \
  --out r3n8_tilings.txt
```

## Current auto profiles on a 96c/192t system

```text
throughput:
  threads                  160
  scheduler                static-hybrid
  compat-cache-policy      lru
  worker-compat-cache      64
  affinity                 none
  hot-row prewarm          top 512 rows if a profile exists

balanced:
  threads                  144
  scheduler                static-hybrid
  compat-cache-policy      lru
  worker-compat-cache      0
  affinity                 none

interactive:
  threads                  96
  scheduler                static-hybrid
  compat-cache-policy      lru
  worker-compat-cache      0
  affinity                 numa when multiple NUMA nodes are detected
```

Known anti-patterns that remain available only as benchmark/reference options:

```text
full work-steal scheduler
parallel-depth 2
compact or spread affinity on the tested Threadripper system
thread-local state cache
labeled-state prefilter
forced unrolled/AVX bitset kernels
canonical augmentation as the production path
```

## Gathering data on another system

The preferred full profiling run is:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix full \
  --gather-cache-mode warm-profiled \
  --gather-warm-solutions 10000 \
  --gather-solutions 10000
```

This writes JSONL records to `perf_data.txt` in the current directory.  Summarize:

```bash
python3 scripts/summarize_perf_data.py perf_data.txt
```

For a faster first-pass profile:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix core \
  --gather-cache-mode warm-profiled \
  --gather-warm-solutions 3000 \
  --gather-solutions 3000
```

For raw state-throughput rather than time-to-tilings:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix core \
  --gather-cache-mode shared \
  --gather-states 1000000 --gather-solutions 0
```

Send back `perf_data.txt` plus the first memory/resource plan printed by the run.

## Hot compatibility rows

`--auto` now records hot compatibility rows automatically to:

```text
<cache-dir>/hot_compat_rows_n8.txt
```

Future `--auto` runs reuse the top 512 rows if the file exists.  Manual control:

```bash
--record-hot-compat-rows hot_rows.txt
--prewarm-hot-compat-rows hot_rows.txt --prewarm-hot-compat-limit 512
```

See:

```text
docs/PERF_GATHERING.md
docs/PERF_FEEDBACK_V4_2026_04_27.md
docs/AUTO_TUNING.md
docs/CACHE_AFFINITY_AND_LAYOUT_2026.md
```


## Experimental Delta(3,9) wide-mask path

`n=9` is now supported through a separate execution path so that the highly tuned
`n<=8` code remains unchanged.  The reason is structural: \(\Delta(3,8)\) has
`C(8,3)=56` vertices and fits in a single `uint64_t`, while \(\Delta(3,9)\) has
`C(9,3)=84` vertices and needs a 128-bit cell-vertex mask.

The package does **not** include a full `allr3n9.txt` reference file.  To run
`n=9`, place a Sage-generated or otherwise validated file named `allr3n9.txt` in
`data/` or pass its directory with `--data-dir`.

Example validation/cache-building command once `allr3n9.txt` is available:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --validate --build-cache-only
```

A first bounded search run:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --auto-profile throughput \
  --state-cache bounded --state-cache-mb 65536 \
  --compat-mem-budget-mb 65536 \
  --progress-interval 1000000 \
  --out r3n9_tilings.txt
```


### Gathering performance data for Delta(3,9)

The `n=9` path now supports `--gather-data`.  Because the full `n=9` search
space is not yet tuned, start with **state-bounded** benchmarks; solution-bounded
benchmarks can be used after the first data shows that early solutions are found
quickly.

Recommended first run:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix core \
  --gather-states 100000 --gather-solutions 0
```


### n=9 gather-data safety update

The default `n=9` gather matrix intentionally omits the former
`n9_labeled_no_final_quotient_reference` experiment.  On realistic `n=9` input
files that row can run for hours while the other probes finish quickly, so it is
not useful for tuning `--auto`.  Labeled search remains available explicitly, but
routine profiling now focuses on thread counts, state-cache budgets,
compatibility-cache budgets, forced propagation, and no-state-cache reference
behavior.

Fuller `n=9` tuning matrix:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix full \
  --gather-states 100000 --gather-solutions 0
```

If early solutions are plentiful, also run:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix core \
  --gather-solutions 1000
```

The `n=9` gather records use `"engine":"n9-wide"` and include the fields
`orbits`, `images`, `vertices`, `compat_row_capacity`, `state_cache_max`, and
`quotient_final_dedup`.  They intentionally benchmark the current wide engine's
root-static scheduler, thread count, compatibility-cache budget, state-cache
budget, and forced-propagation choices.  These are the parameters we need before
designing a more aggressive `n=9` scheduler.

Important implementation details for `n=9`:

```text
cell vertex masks                  Mask128 = two uint64_t limbs
hypersimplex vertices              84
permutations                       9! = 362880
subset masks                       uint16_t, since ground-set subsets use 9 bits
partial-state cache                labeled exact cache
final quotient deduplication       exact brute S_9 canonical key
face tests                         same facet-closure criterion as n<=8
```

The `n=9` path is deliberately isolated.  It does not replace the `uint64_t`
engine used for `n=4,...,8`, and therefore should not degrade the measured `n=8`
performance profile.

## n=9 gather-data matrix change

After this change the 96c/192t full `n=9` matrix has 32 default experiments instead of 33.  The removed row is `n9_labeled_no_final_quotient_reference`, which field testing showed could run for hours while the earlier probes finished quickly.  This keeps `--gather-data` useful as a fast feedback tool for tuning `n=9 --auto`.


## Delta(3,9) auto tuning update

The `n=9` path is isolated from the measured-fast `n<=8` engine.  It uses
`Mask128` because `C(9,3)=84` hypersimplex vertices do not fit in a `uint64_t`.

The first real n=9 dual-Threadripper gather run had:

```text
orbits                 901
labeled images         70,731,920
best first-pass thread count near 128
```

So the n=9 `--auto` profile now differs from the n=8 profile:

```text
n=8 throughput: 160 static-hybrid workers on 96c/192t
n=9 throughput: 128 root-static wide-engine workers on 96c/192t
```

Recommended n=9 run after placing a validated `data/allr3n9.txt`:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --auto-profile throughput \
  --progress-interval 1000000 \
  --out r3n9_tilings.txt
```

Recommended n=9 data-gathering run:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix core \
  --gather-states 100000 --gather-solutions 0
```
