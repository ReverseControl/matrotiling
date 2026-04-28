# Regression history and current feedback tuning

Historical note: this file documents why the packed/work-steal auto profile was demoted.  A later `perf_data.txt` run promoted profile-dependent auto defaults: balanced uses static-hybrid, throughput uses static with modest SMT oversubscription, and interactive uses static with headroom.  See `docs/AUTO_TUNING_FEEDBACK_2026_04.md` for current defaults.

# Regression fix: restoring the measured-fast production path

This package fixes a performance regression observed on a dual Threadripper
system with 192 logical CPUs / 96 physical cores.

## Observed timings from user runs

The earlier `alexeev_r3_cpp_parallel_stream_fix` build reached about 10,319
printed `n=8` tilings in roughly 1 minute 24 seconds with:

- static root-task splitting,
- 96 worker threads,
- LRU compatibility cache,
- direct streaming output,
- the older exact string-key state cache.

Later builds changed several things at once:

- `--auto` silently chose the work-stealing scheduler;
- `--auto` chose the direct compatibility cache;
- the default state-cache backend was changed to a packed open-addressed backend;
- optional pilot mode could spend time on a preliminary run and then retune toward the slower profile;
- `--auto` used 88 threads in balanced mode on the same 96-physical-core machine.

Those changes were mathematically safe, but the combined timing was worse:
about 3 minutes 20 seconds for the feedback-tuned work-stealing build to reach
the same point, and worse again for the packed/pilot build in the reported run.

## Diagnosis

The regression is not caused by the matroid-tiling mathematics.  The enumeration
target is unchanged: face-fitting collections of rank-3 matroid base polytopes in
`Delta(3,n)` with the same coverage and volume tests.

The regression is caused by moving the production `--auto` path away from the
measured-fast DFS traversal.  Work stealing, packed caches, and pilot feedback are
good research tools, but in the available timing data they do not yet beat the older
static search path for early `n=8` throughput.

In particular, early `n=8` search already finds many solutions in the first root-task
frontier.  The overhead of dynamic splitting, extra queue operations, more scheduler
bookkeeping, and the packed cache's probing/arena management outweighed their
benefits in the measured run.

## What changed in this fixed package

`--auto` now again uses the speed-first production profile:

- `scheduler = static`;
- `compat-cache-policy = lru`;
- compatibility budget on 512 GiB systems returns to 64 GiB;
- state-cache budget on 512 GiB systems returns to the 64 GiB measured-fast baseline;
- balanced `--auto` uses physical-core count on high-core systems;
- async output is not enabled silently;
- work stealing remains available only when explicitly requested;
- the state cache uses the older exact sharded string-key backend by default.

The packed backend idea is deliberately not used as the production default in this
package.  It reduced theoretical memory overhead but was slower in the supplied
measurements.  Exactness is unchanged: all partial-state keys are still exact
canonical keys, and hashes are never used as a lossy acceptance criterion.

## Recommended commands

Fast production default:

```bash
./build/alexeev_r3_cpp 8 --auto --out r3n8_tilings.txt
```

Dedicated throughput run:

```bash
./build/alexeev_r3_cpp 8 --auto --auto-profile throughput \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

If you want to experiment with the slower research scheduler:

```bash
./build/alexeev_r3_cpp 8 --auto \
  --scheduler work-steal \
  --compat-cache-policy direct \
  --async-output --flush-every 1024 \
  --out r3n8_tilings_worksteal.txt
```

The work-stealing mode is retained for future tuning, but it is no longer the
production default.

## Validation

The known small quotient counts are still:

- `n=4`: 1;
- `n=5`: 3;
- `n=6`: 26.

These are checked by:

```bash
./build/alexeev_r3_cpp --self-test --data-dir data --threads 4 --count-only
```
