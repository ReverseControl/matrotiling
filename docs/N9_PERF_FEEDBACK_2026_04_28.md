# n=9 wide-engine performance feedback, 2026-04-28

This update uses the first real `Delta(3,9)` gather-data file from the
dual Threadripper system.

The run was state-bounded:

```text
n=9
engine                 n9-wide
vertices               84
orbits                 901
labeled images         70,731,920
max_states             10,000 per experiment
matrix                 full
cache_mode             shared
```

The most useful observations were:

```text
1. 128 threads was the best first-pass point.
   48  threads: ~104.7 s
   96  threads: ~90.2 s
   128 threads: ~85.3 s
   144 threads: ~89.6 s
   160 threads: ~90.8 s
   176 threads: ~94.9 s
   192 threads: ~110.0 s

2. Force propagation must stay enabled.
   The no-force-propagation reference was much slower.

3. The compatibility cache matters.
   The 64 GiB compatibility budget was better than the smaller-budget
   references in the first full matrix.

4. The labeled partial-state cache did not yet show duplicate hits in the first
   10,000 accepted states, but it remains enabled because later/deeper phases may
   repeat labeled states.  It is exact and only prunes work.

5. n=9 should not inherit the n=8 throughput profile.
   For n=8 the best profile used 160 static-hybrid workers.  For n=9, the wide
   path currently uses root-static scheduling and the best measured point is 128
   workers on this system.
```

## Updated auto defaults for a 96c/192t system

```text
throughput:   128 threads, root-static wide engine, 64 GiB compat cache,
              64 GiB state cache, force propagation on
balanced:     128 threads, same as throughput
interactive:   96 threads, same caches, more CPU headroom
memory-saver:  48 threads, smaller state cache
```

These are defaults only.  The user can override any of them explicitly.

## Updated gather-data design

The n=9 full matrix now concentrates near the observed optimum:

```text
96, 112, 120, 128, 136, 144, 152, 160 threads
```

and keeps the known regression sentinels:

```text
176, 192 threads
state-cache none
no force propagation
very small compatibility-cache budget
```

The matrix also adds a first NUMA-affinity probe at the 128-thread point.  The
n=8 data showed that compact/spread affinity can hurt, so n=9 initially tests only
`none` and `numa`.

## Recommended n=9 profiling commands

Fast first pass:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix quick \
  --gather-states 10000 --gather-solutions 0
```

Focused core pass:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix core \
  --gather-states 100000 --gather-solutions 0
```

Full pass:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix full \
  --gather-states 100000 --gather-solutions 0
```

Then summarize:

```bash
python3 scripts/summarize_perf_data.py perf_data.txt
```

## Important limitation

The current n=9 path performs exact final quotient deduplication under `S_9`, but
the partial-state cache is labeled rather than quotient-canonical.  This avoids a
full `9!` canonicalization at every partial state.  It may repeat work, but it does
not undercount final quotient tilings.
