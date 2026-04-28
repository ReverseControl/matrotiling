# Auto tuning quick reference

`--auto` detects logical CPUs, physical cores when possible, memory, and NUMA CPU
lists.  It then selects a benchmark-gated production profile.  The current built-in
profiles are based on repeated n=8 gather-data runs.

On a 96c/192t dual Threadripper-class system:

```text
throughput : 160 threads, static-hybrid, LRU, worker compatibility cache 64
balanced   : 144 threads, static-hybrid, LRU
interactive:  96 threads, static-hybrid, LRU, NUMA affinity if available
```

All profiles keep:

```text
state-cache quotient DFS
parallel-depth 1
global exact state cache
binary canonical state keys
force propagation
bitset-kernel auto
```

`--auto` records hot compatibility rows to `<cache-dir>/hot_compat_rows_n8.txt`
and uses the top 512 rows on later runs if the file exists.

To tune another system:

```bash
./build/alexeev_r3_cpp 8 --auto --gather-data --gather-matrix full \
  --gather-cache-mode warm-profiled --gather-warm-solutions 10000 \
  --gather-solutions 10000
python3 scripts/summarize_perf_data.py perf_data.txt
```

To apply local data:

```bash
./build/alexeev_r3_cpp 8 --auto --auto-profile learned \
  --auto-learn perf_data.txt --out r3n8_tilings.txt
```

---

# 2026-04-27 v3 auto-tuning update

Latest warm-profiled data promoted one additional setting: `--worker-compat-cache-size 64`
for the throughput profile.  On the 96c/192t Threadripper system, the fastest current
10K-solution row is now:

```text
threads_160_static_hybrid_lru_worker_cache_64
elapsed_sec   63.576
solutions/sec 157.292
```

Therefore `--auto-profile throughput` now selects:

```text
threads=160
scheduler=static-hybrid
compat-cache-policy=lru
worker-compat-cache-size=64
affinity=none
hot compatibility prewarm limit=512 if a hot-row profile file exists
```

Balanced and interactive profiles stay more conservative because worker-cache 64 did not
clearly beat their no-worker-cache rows:

```text
balanced:    threads=144, static-hybrid, lru, worker-cache=0
interactive: threads=96,  static-hybrid, lru, worker-cache=0, affinity=numa if available
```

`--auto` never performs blind compatibility prewarm.  It only reuses an existing hot-row
profile, e.g. `./perf_hot_compat_rows.txt` or `.alexeev_cache/perf_hot_compat_rows.txt`.

See `docs/PERF_FEEDBACK_V3_2026_04_27.md`.

# Auto tuning and standard configurations

`--auto` is the production convenience mode. It is empirical: profiles are promoted only after the `--gather-data` benchmarks show that they beat the current reference path.

## Latest feedback incorporated

The latest feedback data from the 192-logical / 96-physical-core / 512 GB dual Threadripper system showed:

```text
best observed 10K-solution profile:
  threads_160_static_hybrid_lru
  elapsed_sec   about 68.1
  solutions/sec about 146.8

strong balanced profile:
  144-thread static-hybrid + lru

strong interactive profile:
  96-thread static-hybrid + lru + numa affinity when NUMA nodes are detected

known regressions:
  full work-steal
  parallel-depth 2
  compact affinity
  spread affinity
  thread-local state cache
  labeled-state prefilter
  forced unrolled/AVX bitset kernels
```

The tuner therefore keeps the exact state-cache quotient DFS and depth-1 traversal, but changes the high-core defaults:

```text
throughput:
  threads = min(logical_cpus - 32, ceil(5 * physical_cores / 3))
  scheduler = static-hybrid
  compat-cache-policy = lru
  affinity = none

balanced:
  threads = min(logical_cpus - 48, ceil(3 * physical_cores / 2))
  scheduler = static-hybrid
  compat-cache-policy = lru
  affinity = none

interactive:
  threads = physical_cores on SMT systems
  scheduler = static-hybrid
  compat-cache-policy = lru
  affinity = numa when multiple NUMA CPU lists are detected
```

On a 96c/192t system, this gives approximately:

```text
throughput  160 threads
balanced    144 threads
interactive  96 threads
```

## Commands

Dedicated throughput run:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile throughput \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

Balanced run:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile balanced \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

Interactive run:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile interactive \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

Learned run from local benchmark data:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile learned --auto-learn perf_data.txt \
  --out r3n8_tilings.txt
```

## Safety

Auto tuning changes only performance parameters: thread count, scheduler mode, cache policy, affinity, and similar systems choices. It does not change the mathematical acceptance test. A tiling is still required to satisfy pairwise face-fitting, full vertex coverage, volume equality, and final quotient deduplication.
