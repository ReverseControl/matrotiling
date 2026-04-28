# Cache-affinity and data-layout optimization update

This implementation adds eleven benchmark-gated optimization families around the
measured-fast production path.  None of these changes alters the mathematical
enumeration problem: tilings are still face-fitting rank-3 matroid base-polytopal cells in
\(\Delta(3,n)\), checked by compatibility, coverage, volume, and final canonical
deduplication.

## Production principle

The latest perf feedback showed that the best path is still:

```text
generation-mode state-cache
parallel-depth 1
static or static-hybrid scheduling
legacy-speed exact sharded state cache
```

The following knobs are therefore **available for testing** and included in
`--gather-data`, but not all are automatic defaults.

## 1. CPU affinity and NUMA detection

```bash
--affinity none|compact|spread|numa
--numa-policy none|first-touch|local-shards
```

Linux builds read `/sys/devices/system/node/node*/cpulist`.

Modes:

```text
none     do not pin worker threads
compact  use OS CPU order
spread   distribute workers over the available CPU list
numa     round-robin workers over NUMA-node CPU lists
```

Worker-local DFS workspaces are initialized after the worker starts; when affinity is
enabled this gives a first-touch effect for those workspaces.  The `local-shards` NUMA
policy is accepted for benchmarking but the production exact global state cache remains
the default.

## 2. Cache-line padding

Hot shared shard headers and row states are cache-line aligned where practical.  This is
intended to reduce false sharing in the state-cache and compatibility-cache metadata.

## 3. Hot compatibility row recording and profiled prewarm

```bash
--record-hot-compat-rows hot_rows.txt
--prewarm-hot-compat-rows hot_rows.txt
--prewarm-hot-compat-limit 512
```

The recorder writes rows sorted by hit count:

```text
# row_id hits
12345 991
...
```

This is not a correctness input.  It is only a startup optimization for later runs.  It avoids
blindly prewarming thousands of rows and instead prewarms rows observed to be hot.

## 4. Bitset kernel dispatch

```bash
--bitset-kernel auto|scalar|unrolled|avx2|avx512
```

The `unrolled` and `auto` modes use manually unrolled 64-bit word loops for
`count_intersection`, bounded intersection counts, and dense `AND` kernels.  `avx2` and
`avx512` are accepted as gather-data variants and safely fall back to the unrolled scalar
path unless the build/toolchain is extended with explicit intrinsics.

## 5. Structure-of-arrays image metadata

The search already used separate `vmask_list` and `vol_list`.  This update adds more
explicit SoA fields:

```text
vmask_list
vol_list
vertex_count_list
orbit_index_list
perm_index_list
```

The hot search path reads vertex masks and volumes without forcing orbit/perm label data
into cache.

## 6. Fixed-depth DFS workspace reservation

In sha-volume mode for \(n=8\), total volume is \(25\), so DFS depth is bounded by volume.
Each worker reserves per-depth vectors once, reducing repeated allocation in recursion.

## 7. Binary canonical state keys

```bash
--state-key-format binary|hex
```

The production default is `binary`.  Canonical state keys are exact; the binary form stores
raw 64-bit cell vertex masks and avoids printable hex expansion.

## 8. Worker-local tiny compatibility cache

```bash
--worker-compat-cache-size K
```

A worker can keep a small cyclic cache of immutable compatibility row representations.
This may reduce global compatibility-cache lookups.  It is off by default until perf data
shows it wins on a given machine.

## 9. Profile-guided root ordering

```bash
--record-root-performance root_perf.txt
--use-root-performance root_perf.txt
```

This records an approximate root-productivity signal and can use it later as an ordering
hint near the root.  Ordering changes only traversal order, not correctness.

## 10. Expanded gather-data matrix

`--gather-data --gather-matrix full` now includes:

```text
affinity none/compact/spread/numa
bitset kernel auto/scalar/unrolled/avx2/avx512
state key binary/hex
worker compatibility cache sizes 0/16/64
state-cache scope global/thread-local
static/hybrid scheduler variants
compatibility lru/lru-notouch/direct variants
thread sweeps
known anti-pattern sentinels
```

Use `warm-profiled` to reduce cache-order bias:

```bash
--gather-cache-mode warm-profiled --gather-warm-solutions 10000
```

## 11. CMake native / LTO / PGO build profiles

```bash
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release \
  -DALEXEEV_ENABLE_NATIVE=ON -DALEXEEV_ENABLE_LTO=ON
```

PGO workflow:

```bash
cmake -S . -B build-pgo-gen -DCMAKE_BUILD_TYPE=Release \
  -DALEXEEV_ENABLE_NATIVE=ON -DALEXEEV_ENABLE_PGO_GENERATE=ON
cmake --build build-pgo-gen -j
./build-pgo-gen/alexeev_r3_cpp 8 --auto --gather-data --gather-matrix quick --gather-solutions 1000

cmake -S . -B build-pgo-use -DCMAKE_BUILD_TYPE=Release \
  -DALEXEEV_ENABLE_NATIVE=ON -DALEXEEV_ENABLE_LTO=ON -DALEXEEV_ENABLE_PGO_USE=ON
cmake --build build-pgo-use -j
```

## Suggested next feedback run

```bash
./build/alexeev_r3_cpp 8 --data-dir data --cache-dir .alexeev_cache \
  --auto --gather-data --gather-matrix full --gather-cache-mode warm-profiled \
  --gather-warm-solutions 10000 --gather-solutions 10000
python3 scripts/summarize_perf_data.py perf_data.txt
```

Send back the resulting `perf_data.txt`; the summarizer now includes affinity, bitset,
state-key, and worker-cache fields.
