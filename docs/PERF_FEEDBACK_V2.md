# Performance feedback update: dual Threadripper n=8 runs

This release incorporates the second round of `perf_data.txt` / `perf_data_v2.txt` feedback
from the 192-logical-CPU dual Threadripper system.

## What the data showed

For time-to-10,000 tilings, the reliable solution-based data favored the existing fast
state-cache DFS backbone.  The strongest safe profiles were:

| profile | scheduler | threads | compat policy | effect |
|---|---:|---:|---|---|
| `threads_128_static_lru` | static | 128 | lru | best measured throughput run in the 10K benchmark |
| `static_hybrid_lru_notouch` | static-hybrid | 96 | lru-notouch | best balanced 96-thread profile in the v2 10K benchmark |
| `static_hybrid_direct` | static-hybrid | 96 | direct | close to lru-notouch, lower compatibility time but not always lower elapsed time |
| `threads_80_static_lru` | static | 80 | lru | good interactive profile with OS/browser headroom |

The data also confirmed two anti-patterns:

* full `work-steal` has good core utilization but poor time-to-10K because it explores a less
  solution-dense region earlier;
* `parallel-depth 2` is a severe regression and is never selected by `--auto`.

## Auto profile changes

`--auto-profile balanced` now uses:

```text
threads                  physical cores
scheduler                static-hybrid
compat-cache-policy      lru-notouch
parallel-depth           1
duplicate-local-cache    65536
```

`--auto-profile throughput` uses:

```text
threads                  about 4/3 physical cores on SMT machines
scheduler                static
compat-cache-policy      lru
parallel-depth           1
```

On the 96-physical / 192-logical Threadripper system this means about 128 threads.

`--auto-profile interactive` remains the lower-contention profile:

```text
threads                  physical cores minus a large reserve
scheduler                static
compat-cache-policy      lru
```

## New learned profile mode

The program can now read a previous `perf_data.txt` and choose the best safe production
profile from it:

```bash
./alexeev_r3_cpp 8 --auto --auto-profile learned --auto-learn perf_data.txt --out r3n8_tilings.txt
```

The learned selector only chooses production-safe records:

* `parallel_depth == 1`;
* scheduler is not full `work-steal`;
* labeled-state prefilter is off;
* anti-pattern names such as `depth2`, `work_steal`, and `prefilter` are rejected;
* reliable solution records need at least 5000 solutions;
* if no solution record exists, a state-throughput record with at least 50000 states may be used.

Learning changes only parameters such as thread count, scheduler, compatibility policy, and
cache choices.  It does not change the mathematical search or acceptance criteria.

## Gather-data updates

`--gather-data` now includes more variants aimed at the observed performance boundary:

* static LRU, LRU-notouch, and direct policies across a thread sweep;
* static-hybrid LRU, LRU-notouch, and direct at 96, 128, 144, and 160 thread scales when available;
* repeat sentinels at the end of the full matrix to expose cache-order bias;
* `--gather-randomize-order` to shuffle experiment order with a fixed seed.

Recommended next data run:

```bash
./alexeev_r3_cpp 8 --auto --gather-data --gather-matrix full --gather-cache-mode shared --gather-solutions 10000
```

For cold-cache fairness:

```bash
./alexeev_r3_cpp 8 --auto --gather-data --gather-matrix core --gather-cache-mode reset --gather-solutions 10000
```

Then summarize:

```bash
python3 scripts/summarize_perf_data.py perf_data.txt
```

The summarizer now prints the best safe production profile and an equivalent command.
