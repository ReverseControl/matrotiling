# Algorithm notes

## Mathematical object

The program enumerates rank-3 matroid tilings of \(\Delta(3,n)\).  A cell is represented
by the set of hypersimplex vertices, i.e. the 3-subsets satisfying the cell's essential
inequalities.

A tiling is accepted only when:

```text
all chosen cells are pairwise face-compatible
covered_vertex_mask == all_vertices_mask
volume_sum == total_volume
final canonical tiling key is new
```

## Representations

```text
ground subset                    uint16_t / uint32_t
hypersimplex vertex              3-subset mask
cell vertex set                  uint64_t over C(n,3) vertices
image-index sets                 DynBitset
partial tiling state key         sorted tuple of cell vertex masks
```

For `n=8`, `C(8,3)=56`, so every cell vertex set fits in `uint64_t`.

## Face-fitting

Two cells are compatible when their vertex intersection is empty or a face of both cells.
The large-case face test avoids polyhedral libraries:

1. start with the whole cell vertex set;
2. intersect with every coordinate or essential facet containing the intersection;
3. accept iff the closure equals the intersection.

For `n<=6`, the validator compares this with representative face-set pullback.

## Production search

Production mode is:

```text
--generation-mode state-cache
```

It uses quotient DFS with:

* root-orbit symmetry breaking;
* volume filtering;
* coverage-feasible branching on the rarest uncovered hypersimplex vertex;
* exact canonical partial-state keys;
* full or bounded state cache;
* exact final canonical solution keys.

The uncovered-vertex branch rule is safe in this DFS because every completion must cover
the chosen missing vertex eventually.

## Canonical augmentation

Canonical augmentation is still implemented:

```text
--generation-mode canonical
```

It uses a canonical construction path rather than a global partial-state cache.  This is
memory-safe but slower for `n=7` and usually not the production choice.  It remains useful
as a validation or low-memory mode.

## Compatibility cache

For `n<=6`, compatibility rows are precomputed.  For `n=7,8`, rows are lazy and LRU
cached.  The two-hit filter only performs face tests against images sharing at least two
hypersimplex vertices with the row cell.

Rows in the LRU may be stored densely or as sparse incompatible-index lists.

## Volumes

`--volume-mode sha` uses `(n-3)^2`.
`--volume-mode lattice` uses the lattice volume of the uniform orbit from the input file.

Volume pruning is only a necessary condition; it never changes the set of accepted tilings.
