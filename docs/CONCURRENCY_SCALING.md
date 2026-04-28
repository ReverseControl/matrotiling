# Concurrency scaling

The `n=8` production search is an irregular DFS with highly unequal subtrees.  Root-orbit
splitting gives only 186 seed tasks for about 96 physical cores, and these tasks are very
imbalanced.  The large-core path therefore benchmarks both static and dynamic approaches.  The latest `perf_data.txt` showed that full work stealing had worse time-to-10K tilings, so production auto uses static-hybrid late donation for balanced mode and static depth-1 for throughput/interactive modes.

## Work-stealing scheduler

Enable with:

```bash
--scheduler work-steal
```

When explicitly using `--scheduler work-steal`, typical knobs are:

```text
--task-target-per-thread 128
--split-max-depth 5
--split-min-candidates 8
```

A worker processes a DFS node locally until the ready queue is below the target and the
node has enough children.  It then donates child subtrees to the global queue.  This does
not change the mathematical search tree; it changes only scheduling.

Progress counters:

```text
active_workers       workers currently inside DFS tasks
peak_active_workers  maximum observed active workers
ready_tasks          tasks waiting in the dynamic queue
tasks_created        tasks ever enqueued
tasks_completed      tasks finished
tasks_donated        child subtrees donated by DFS
dynamic_splits       nodes where donation/splitting occurred
scheduler_waits      times workers waited for work
```

Interpretation:

* `ready_tasks` near zero and many `scheduler_waits`: split more aggressively.
* `ready_tasks` large but low CPU utilization: suspect state-cache/compat-cache locks or memory bandwidth.
* `peak_active_workers` far below `threads`: split earlier or use prefix/process sharding.

## Recommended high-core command

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile balanced \
  --async-output --flush-every 1024 \
  --out r3n8_tilings.txt
```

Explicit dual Threadripper balanced command:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile balanced \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

Explicit dual Threadripper throughput command:

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --auto-profile throughput \
  --progress-interval 1000000 \
  --out r3n8_tilings.txt
```

## Process sharding

For NUMA systems, multiple processes can outperform one large process because each process
has separate state, compatibility, and output caches.  Use prefix tasks for better balance:

```bash
./build/alexeev_r3_cpp 8 --auto \
  --prefix-task-target 20000 --prefix-max-depth 2 \
  --write-prefix-tasks n8_prefix_tasks.jsonl
```

Then:

```bash
SHARDS=8 THREADS_PER_SHARD=12 \
STATE_MB_PER_SHARD=8192 COMPAT_MB_PER_SHARD=8192 MEM_LIMIT_MB_PER_SHARD=48000 \
./scripts/run_n8_prefix_shards_example.sh n8_prefix_tasks.jsonl
```

Prefix shards are weighted by a scheduling estimate in the JSONL file and assigned by
greedy bin packing to reduce imbalance.

## Thread count

For SMT machines, benchmark physical-core scale and modest oversubscription.  The supplied 96-physical/192-logical data showed 128 static threads winning time-to-10K, while 80 threads remained a good interactive compromise.  On a 192 logical /
96 physical system:

```text
interactive: 84-ish threads
balanced:    88-ish threads
throughput:  96 threads
```

Test 80, 88, 96, and 128 only after checking `active_workers`, `ready_tasks`, and RSS.
If utilization is poor, increasing threads is usually less useful than increasing dynamic
task supply or reducing cache-lock contention.


## Pilot-informed scheduling

The feedback pilot records `peak_active_workers`.  If the pilot shows that too few workers were
active, auto tuning increases dynamic work generation:

```text
task_target_per_thread increases
split_max_depth increases
split_min_candidates decreases
```

This directly addresses the long-tail DFS problem: root branches have very different sizes, so a
small static root frontier is insufficient on 64- to 192-CPU systems.  Work stealing remains the
production scheduler for n=8.

For NUMA machines, prefer prefix/process sharding if a single process cannot keep cores busy.
The prefix sharding scripts now document `numactl` examples when it is available.



## Regression-fix note

As of the regression-fixed package, `--auto` uses the measured-fast static scheduler and LRU compatibility cache by default.  Work stealing, direct compatibility caching, packed-cache experiments, and auto pilot feedback remain available explicitly, but they are no longer production defaults because user timing showed slower time-to-solution on the 192-logical-CPU `n=8` run.
