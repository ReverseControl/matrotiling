# Delta(3,9) wide-mask support

This build adds an experimental but exact wide-mask path for \(\Delta(3,9)\).

## Why a separate path is needed

For rank 3, hypersimplex vertices are 3-subsets of the ground set.

```text
n=8: C(8,3)=56  -> one uint64_t cell vertex mask
n=9: C(9,3)=84  -> needs more than 64 bits
```

The production `n<=8` engine keeps its one-word `uint64_t` representation.  The
`n=9` engine uses:

```cpp
struct Mask128 {
    uint64_t lo;
    uint64_t hi;
};
```

This is intentionally equivalent to a two-limb `__uint128_t` representation, but
it is more portable and easier to serialize.  On GCC/Clang, `Mask128` exposes
`to_native()` and `from_native()` helpers backed by `unsigned __int128`; the hot
path currently keeps explicit limbs because it avoids compiler-dependent layout and
keeps serialization/canonical-key construction simple.  A future AVX or full
native-`__uint128_t` specialization can be added behind this same abstraction.

## What is supported

The `n=9` path supports:

```text
loading allr3n9.txt JSONL orbit data
rank-3 hypersimplex precomputation with 84 vertices
labeled image generation under S_9
by-vertex image membership bitsets
facet-closure face tests
lazy compatibility rows
bounded exact labeled partial-state cache
exact final S_9 canonical quotient deduplication
streamed text output
```

The face-fitting test is the same structural test as for `n<=8`: for two cells,
their vertex-set intersection must be empty or a face of both cells.  A face is
recognized as the closure under coordinate and essential inequality equalities.

## What input is required

This package does not contain a full `allr3n9.txt`.  The program expects a file
with the same format as the included `allr3n8.txt`:

```json
{"ineqs": [...], "n": 9, "orbit_id": 0, "volume": 36,
 "volume_sha": 36, "volume_lattice": ...}
```

For sha-volume, the uniform cell should have volume:

```text
(n-3)^2 = 36
```

## Exactness convention

The current `n=9` engine avoids brute \(S_9\) canonicalization at every partial
state because that would be too expensive.  Instead:

```text
partial states:
  cached by exact labeled image-index sets

final tilings:
  deduplicated by exact brute S_9 canonical key
```

This may repeat more work than the `n=8` quotient engine, but it does not
undercount final quotient tilings.  The search still enforces:

```text
pairwise face compatibility
volume sum
full hypersimplex vertex coverage
final exact quotient canonical deduplication
```

## First commands

After placing a full `allr3n9.txt` in `data/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/alexeev_r3_cpp 9 --data-dir data --validate --build-cache-only
```

Then run a first search:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --auto-profile throughput \
  --state-cache bounded --state-cache-mb 65536 \
  --compat-mem-budget-mb 65536 \
  --out r3n9_tilings.txt
```

## Keeping n=8 performance

The `n=9` code is not templated into the old hot path.  This was a deliberate
performance choice.  The measured-fast `n=8` representation remains:

```text
uint64_t cell masks
n=8-specific dynamic bitset operations
static-hybrid depth-1 production scheduler
hot compatibility rows and worker-local compatibility cache
```

The only dispatch change is in `main.cpp`: if `n==9`, the program calls the
wide-mask engine before constructing the old `HypersimplexR3`, whose limit
remains `n<=8`.


## Gather-data support

The wide engine now supports `--gather-data`.  Start with a state-bounded run:

```bash
./build/alexeev_r3_cpp 9 --data-dir data --cache-dir .alexeev_cache_n9 \
  --auto --gather-data --gather-matrix core \
  --gather-states 100000 --gather-solutions 0
```

This writes `perf_data.txt` with records marked `"engine":"n9-wide"`.  These
records are meant to tune `n=9` separately from `n=8`; the `n<=8` engine and its
measured-fast defaults are not affected.
