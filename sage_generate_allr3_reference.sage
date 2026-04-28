#!/usr/bin/env sage
# -*- coding: utf-8 -*-
"""
Generate Sage reference files allr3nX.txt for X=3..10.

These files are consumed by the *pure Python 3.12* tiling enumerator.

What is recorded per orbit (one JSON object per line):
  - orbit_id: index within the filtered orbit list (loopless + connected)
  - n: ground set size
  - ineqs: Alexeev-style *essential inequalities* for the rank-3 matroid polytope
           inside Δ(3,n), in the form x(S) <= k, encoded as:
             {"subset_mask": <bitmask of S in [0,2^n)>, "k": <int>}
           where we keep inequalities coming from *nondegenerate flats* and
           only those with k < |S| (i.e. r(S) < |S|).
  - volume_sha: Alexeev's sha-volume for b=1 in rank 3, i.e.
           vol_sha(P) = (K_X + Σ B_i + D)^2 on the component corresponding to P,
           satisfying vol_sha(Δ(3,n)) = (n-3)^2 and additive across a tiling.
           For rank 3 this can be computed purely from the matroid:
             let d = number of parallel classes (size of the simplification),
             and for each rank-2 flat/point p let m_p = number of parallel
             classes through p. Then
                 vol_sha = (d-3)^2 - Σ_{m_p>=3} (m_p-2)^2 .
  - volume_lattice: a standard lattice-normalized Euclidean volume of the matroid polytope,
            computed after projecting Δ(3,n) to Z^(n-1) by dropping the last
            coordinate and using (n-1)! * EuclideanVolume.
  - volume: legacy alias for volume_sha (kept for backward compatibility)

Run:
    sage sage_generate_allr3_reference.sage
"""

from __future__ import print_function

import json
import sys

from sage.all import matroids, Polyhedron, factorial


# -----------------------
# Helpers (robust across Sage builds)
# -----------------------

def _groundset_pos(M):
    E = sorted(M.groundset())
    return E, {e: i for i, e in enumerate(E)}


def _subset_mask(subset, pos):
    m = 0
    for e in subset:
        m |= (1 << pos[e])
    return int(m)


def _all_subsets(E):
    """Enumerate all subsets of E as Python sets. For n<=10, at most 1024 subsets."""
    E = list(E)
    n = len(E)
    for mask in range(1 << n):
        S = set()
        for i, e in enumerate(E):
            if (mask >> i) & 1:
                S.add(e)
        yield S


def _closure(M, S, Eset):
    """Closure cl(S) using only rank queries (avoid M.flats(), M.closure())."""
    S = set(S)
    rS = int(M.rank(S))
    out = set(S)
    for e in Eset:
        if e in S:
            continue
        if int(M.rank(S | {e})) == rS:
            out.add(e)
    return out


def _is_flat(M, S, Eset):
    """
    Flat test avoiding M.flats() (which can crash on some Sage builds).

    S is a flat iff for every e not in S we have rank(S ∪ {e}) > rank(S).
    """
    rS = int(M.rank(S))
    for e in Eset:
        if e in S:
            continue
        if int(M.rank(S | {e})) == rS:
            return False
    return True


def uniform_r3n3_if_needed(n, mats):
    """
    Some Sage installations don't return U_{3,3} via AllMatroids(3,3).
    Inject it if needed.
    """
    if n == 3 and len(mats) == 0:
        return [matroids.Uniform(3, 3)]
    return mats


def json_safe(obj):
    """Recursively convert Sage numeric types to plain Python ints for json.dumps."""
    if isinstance(obj, dict):
        return {str(k): json_safe(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [json_safe(x) for x in obj]
    try:
        return int(obj)
    except Exception:
        return obj


# -----------------------
# Core computations
# -----------------------

def nondegenerate_flats_inequalities_rank3(M):
    """
    Return a list of (subset_mask, k) encoding essential inequalities x(S) <= k,
    where S is a nondegenerate flat and k=r(S), and we keep only nontrivial ones
    with k < |S|.
    """
    E, pos = _groundset_pos(M)
    Eset = set(E)
    n = len(E)

    out = []
    for S in _all_subsets(E):
        if len(S) == 0 or len(S) == n:
            continue
        if len(S) == 1:
            continue
        if not _is_flat(M, S, Eset):
            continue

        # Nondegenerate: both restriction and contraction connected.
        comp = Eset - S
        MS = M.delete(comp)          # restriction to S
        if not MS.is_connected():
            continue
        if not M.contract(S).is_connected():
            continue

        k = int(M.rank(S))
        if k == 3:
            continue
        if k == len(S):
            continue

        out.append((_subset_mask(S, pos), k))

    out = sorted(set(out), key=lambda t: (t[1], t[0]))
    return out


def lattice_volume_rank3_projected(M):
    """
    Lattice-normalized Euclidean volume after projecting to Z^(n-1) by dropping the last coordinate.
    """
    E, pos = _groundset_pos(M)
    n = len(E)
    d = n - 1
    last = E[-1]

    verts = []
    for B in M.bases():
        B = set(B)
        v = [0] * d
        for e in B:
            if e == last:
                continue
            v[pos[e]] = 1
        verts.append(v)

    P = Polyhedron(vertices=verts)
    if P.dim() != d:
        return 0

    # (n-1)! * Euclidean volume
    vol = factorial(d) * P.volume()
    return int(round(float(vol)))


def sha_volume_rank3(M):
    """
    Alexeev sha-volume (K+boundary)^2 for b=1 in rank 3.

    Computed combinatorially as:
      vol_sha = (d-3)^2 - Σ_{points} (m_p-2)^2
    where:
      d = number of parallel classes (size of simplification),
      points = rank-2 flats in the simplified matroid,
      m_p = number of parallel classes incident to the point.

    Implemented without using M.flats() for robustness.
    """
    E, _pos = _groundset_pos(M)
    Eset = set(E)
    n = len(E)

    # Union-find for parallel classes: i~j iff rank({i,j})==1.
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def union(i, j):
        ri, rj = find(i), find(j)
        if ri != rj:
            parent[rj] = ri

    for i in range(n):
        for j in range(i + 1, n):
            if int(M.rank({E[i], E[j]})) == 1:
                union(i, j)

    classes = {}
    for i, e in enumerate(E):
        classes.setdefault(find(i), []).append(e)

    class_list = list(classes.values())
    d = len(class_list)
    if d < 3:
        return 0

    # element -> class index
    class_of = {}
    reps = []
    for idx, cls in enumerate(class_list):
        reps.append(cls[0])
        for e in cls:
            class_of[e] = idx

    # Enumerate rank-2 flats in the simplification via closures of pairs of reps.
    flats2 = set()
    for a_i in range(d):
        for b_i in range(a_i + 1, d):
            a = reps[a_i]
            b = reps[b_i]
            if int(M.rank({a, b})) < 2:
                continue
            cl = _closure(M, {a, b}, Eset)
            cl_classes = frozenset(class_of[e] for e in cl)
            if len(cl_classes) >= 2:
                flats2.add(cl_classes)

    sub = 0
    for F in flats2:
        m_p = len(F)
        if m_p >= 3:
            sub += (m_p - 2) * (m_p - 2)

    vol = (d - 3) * (d - 3) - sub
    return int(vol)



def _parse_n_arg(argv):
    """Parse an optional n from argv.

    Supports either:
      - positional:   sage sage_generate_allr3_reference.sage 8
      - flag form:   sage sage_generate_allr3_reference.sage --n 8

    Returns:
      int n if provided, else None.
    """
    n = None
    # First: flag form
    for i, a in enumerate(argv):
        if a in ('--n', '-n') and i + 1 < len(argv):
            try:
                n = int(argv[i + 1])
                return n
            except Exception:
                pass
    # Second: positional first integer
    for a in argv:
        if a.isdigit():
            try:
                return int(a)
            except Exception:
                pass
    return None

def main():
    n_arg = _parse_n_arg(sys.argv[1:])
    if n_arg is None:
        ns = range(3, 11)
    else:
        ns = [n_arg]
    for n in ns:
        out_path = f"data/allr3n{n}.txt"
        print(f"Generating {out_path} ...")

        mats = uniform_r3n3_if_needed(n, list(matroids.AllMatroids(n, 3)))

        recs = []
        orbit_id = 0
        seen_bases = set()

        for M in mats:
            # Deduplicate by basis family just in case (and to handle injected U_{3,3}).
            bkey = tuple(sorted(tuple(sorted(B)) for B in M.bases()))
            if bkey in seen_bases:
                continue
            seen_bases.add(bkey)

            if len(M.loops()) != 0:
                continue
            if not M.is_connected():
                continue

            ineqs = nondegenerate_flats_inequalities_rank3(M)
            vol_lat = lattice_volume_rank3_projected(M)
            vol_sha = sha_volume_rank3(M)

            rec = {
                "n": int(n),
                "orbit_id": int(orbit_id),
                "volume_sha": int(vol_sha),
                "volume_lattice": int(vol_lat),
                "volume": int(vol_sha),  # legacy alias
                "ineqs": [{"subset_mask": int(sm), "k": int(k)} for (sm, k) in ineqs],
            }
            recs.append(json_safe(rec))
            orbit_id += 1

        with open(out_path, "w", encoding="utf-8") as f:
                f.write(f"# rank-3 connected loopless matroid orbits for n={n}\n")
                f.write("# generated by sage_generate_allr3_reference.sage\n")
                for rec in recs:
                    f.write(json.dumps(rec, sort_keys=True) + "\n")

        print(f"  wrote {len(recs)} orbits")


if __name__ == "__main__":
    main()
