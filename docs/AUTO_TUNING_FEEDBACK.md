# Auto-tuning feedback log format

The auto tuner is intentionally empirical.  Bring back progress logs from new machines and
the rules in `docs/AUTO_TUNING.md` can be updated.

## Minimal useful command

```bash
./build/alexeev_r3_cpp 8 --auto --max 100000 \
  --progress-interval 100000 \
  --out n8_probe.txt 2> n8_probe.log
```

For full runs, keep the normal progress interval:

```bash
./build/alexeev_r3_cpp 8 --auto --progress-interval 1000000 \
  --out r3n8_tilings.txt 2> r3n8_run.log
```

## Fields to report

Please include the memory plan and several progress lines containing:

```text
detected logical_cpus
detected physical_cores
detected mem_total_mb
detected mem_available_mb
auto_profile
threads
scheduler
task_target_per_thread
split_max_depth
split_min_candidates
compat_cache_policy
state_cache_mode
state-cache entries/cap
RSS memory guard
states
solutions
duplicate_states
state_cache_size
state_cache_evictions
compat_hits
compat_misses
ready_tasks
active_workers
peak_active_workers
tasks_created
tasks_completed
tasks_donated
dynamic_splits
scheduler_waits
rss_mb
elapsed_sec
```

## Derived ratios

Useful derived ratios:

```text
duplicate_ratio = duplicate_states / (states + duplicate_states)
compat_miss_rate = compat_misses / (compat_hits + compat_misses)
state_mb_per_million = state_cache_est_mb / (state_cache_size / 1e6)
worker_utilization = peak_active_workers / threads
```

Interpretation:

* duplicate ratio above 0.5 means the state cache is a major accelerator;
* tiny compatibility miss rate means compatibility prewarm is not urgent; direct row caching must still beat LRU in controlled gather-data before becoming default;
* low worker utilization means dynamic splitting or process sharding needs more work;
* state-cache evictions with high duplicate ratio mean the state-cache budget should increase if RAM allows.


## 2026-04 feedback note

The latest `perf_data.txt` promoted profile-dependent auto defaults:

```text
balanced     static-hybrid, physical-core count, LRU
throughput   static, about 4/3 physical cores on SMT systems, LRU
interactive  static, physical-core count minus headroom, LRU
```

Full work stealing, static `parallel-depth=2`, direct compatibility caching, and labeled-state prefiltering remain benchmarked but are not defaults unless future controlled gather-data runs show a win.
