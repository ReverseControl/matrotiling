# Gathering performance data

`--gather-data` runs an exact but truncated benchmark matrix and appends JSONL
records to `perf_data.txt` in the current directory.  It is the main workflow for
tuning `--auto` on systems other than the dual Threadripper test machine.

## Recommended full profiling run

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix full \
  --gather-cache-mode warm-profiled \
  --gather-warm-solutions 10000 \
  --gather-solutions 10000
```

Then run:

```bash
python3 scripts/summarize_perf_data.py perf_data.txt
```

Send back `perf_data.txt` and the resource plan printed at startup.

## Faster first pass

Use this on smaller machines or when first checking a new build:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix core \
  --gather-cache-mode warm-profiled \
  --gather-warm-solutions 3000 \
  --gather-solutions 3000
```

## State-throughput benchmark

Time-to-tilings depends on traversal order.  To measure raw search throughput:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix core \
  --gather-cache-mode shared \
  --gather-states 1000000 --gather-solutions 0
```

## Cache modes

```text
shared
  Experiments run sequentially and share warmed in-memory/disk compatibility rows.
  Fastest to run, but later rows benefit from earlier rows.

reset
  Clears in-memory search state between experiments.  More fair, slower.

warm
  Runs a generic warmup before each experiment.

warm-profiled
  Records actual hot compatibility rows during warmup, then prewarms the same
  profile before each experiment.  This is the preferred fair mode for n=8.
```

## Matrix modes

```text
quick
  Small smoke matrix.

core
  Production candidates plus key anti-pattern sentinels.

full
  Core plus thread sweeps, static-hybrid donation sweeps, compatibility policies,
  worker-local compatibility caches, hot-row prewarm limits, affinity variants,
  bitset kernels, and state-key formats.
```

## What the profiler records

Each JSON line includes:

```text
elapsed_sec
cpu_sec
effective_cores
solutions/sec
states/sec
threads
scheduler
compat-cache-policy
affinity
worker-compat-cache-size
prewarm-hot-compat-limit
duplicate ratio
compatibility miss rate
state-cache size / evictions
tasks donated / dynamic splits
forced cells
RSS
```

## How to use the results

1. Sort by `solutions_per_sec` for time-to-output.
2. Sort by `states_per_sec` for raw search throughput.
3. Reject known anti-patterns unless you are explicitly testing them:
   `work-steal`, `parallel-depth > 1`, compact/spread affinity on the tested AMD
   machine, thread-local state-cache scope, labeled-state prefilter, and forced
   unrolled/AVX kernels that do not beat `auto`.
4. Use `--auto-profile learned --auto-learn perf_data.txt` to apply the best safe
   profile from your own file.

## Current focused v4 tests

The full matrix now includes focused rows around the best observed profile:

```text
v4_best_threads_<T>_hybrid_lru_worker64_prewarm512
v4_near_best_threads_<T>_hybrid_direct_worker64_prewarm512
v4_threads_<T>_hybrid_lru_worker64_prewarm2048
v4_threads_<T>_hybrid_lru_worker128_prewarm512
```

These separate the effects of:

```text
worker-local compatibility cache
LRU vs direct compatibility policy
hot-row prewarm limit
high thread count around 160
```

## Using a learned profile

After gathering:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile learned --auto-learn perf_data.txt \
  --out r3n8_tilings.txt
```


## Delta(3,9) wide-engine profiling

`n=9` uses a separate `Mask128` engine because \(\Delta(3,9)\) has
`C(9,3)=84` hypersimplex vertices.  The wide engine now supports
`--gather-data`, but its performance questions are different from the tuned
`n=8` engine:

* the cell vertex mask is two 64-bit limbs;
* image generation uses `9! = 362880` ground-set permutations;
* the current search scheduler is root-static rather than the tuned `n=8`
  static-hybrid scheduler;
* partial-state caching is labeled and exact; final quotient deduplication is
  exact under \(S_9\).

For this reason the first `n=9` profiling runs should usually be state-bounded:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix core \
  --gather-states 100000 --gather-solutions 0
```

A broader matrix is:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix full \
  --gather-states 100000 --gather-solutions 0
```

If solutions are found quickly, also collect a solution-bounded run:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix core \
  --gather-solutions 1000
```

The output is appended to `perf_data.txt` in the current directory.  Send back
that file together with the first memory/resource plan printed by the program.

The current `n=9` matrix compares:

```text
thread counts around physical, SMT, and logical CPU levels
state-cache budgets
compatibility-cache budgets
state-cache disabled as a reference anti-pattern
forced propagation on/off
```

The old `n9_labeled_no_final_quotient_reference` benchmark row was removed from
the default `n=9` gather matrix after field testing showed that it can run for
hours while the preceding probes complete quickly.  It is not part of the fast
feedback loop.  To study labeled search, run it explicitly with `--search-mode
labeled`; do not include it in routine `--gather-data` runs.

Do not compare `n=8` and `n=9` records directly: the engines and object sizes
are different.  Use the `engine` field in the JSON records.
