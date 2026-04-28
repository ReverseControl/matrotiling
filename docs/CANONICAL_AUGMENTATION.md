# Canonical augmentation

Canonical augmentation is implemented but demoted from production default.

```bash
./build/alexeev_r3_cpp 8 --data-dir data --generation-mode canonical
```

## Parent map

For a nonempty state \(S\):

1. canonically relabel \(S\) under the ground-set symmetric group;
2. delete the lexicographically last cell in the canonical tuple;
3. canonicalize the remaining tuple again.

An augmentation \(T \to T \cup \{C\}\) is accepted only if \(T\) is isomorphic to this
canonical parent.

## Why it is slower

The canonical parent test is expensive because the safety-critical canonical key still uses
a full \(S_n\) fallback bounded by `8! = 40320`.  This is exact for `n<=8`, but repeated
many times it is much slower than state-cache DFS on `n=7`.

## Why it remains useful

It avoids the growing partial-state table, so it is useful as:

```text
a validation mode
a low-memory mode
a future platform for a certified graph canonical-labeling backend
```

Production `n=8` should normally use:

```text
--generation-mode state-cache --state-cache bounded
```
