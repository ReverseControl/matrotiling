#!/usr/bin/env sage -python
"""
Compute automorphism group data for rank-3 matroid polytope orbit representatives (Alexeev-style).

This reads the JSONL orbit-rep files produced by:
  sage sage_generate_allr3_reference.sage --n N

and produces:
  1) data/r3nN_auto.txt         (human-readable, backward compatible)
  2) data/r3nN_autinfo.jsonl    (machine-readable, includes generators)

Usage examples (run with Sage's Python):
  sage -python sage_aut_orders.py 8
  sage -python sage_aut_orders.py --n 7
  sage -python sage_aut_orders.py --input data/allr3n6.txt --txt-out data/r3n6_auto.txt --jsonl-out data/r3n6_autinfo.jsonl

The JSONL format is one record per orbit representative:
  {"orbit_id": <int>, "n": <int>, "aut_order": <int>, "aut_gens": [[...],[...],...]}

Conventions:
  - All generators are 0-based one-line permutations of length n.
  - A one-line permutation list `p` means: p[i] = image of i.

Notes:
  - Sage may represent permutation groups either on {0,...,n-1} or {1,...,n}.
    We detect and normalize to 0-based for downstream pure-Python code.
"""

from __future__ import annotations

import argparse
import json
import os
from collections import Counter
from typing import Any, Dict, List, Optional, Tuple

from sage.all import Matroid


def bases_from_inequalities(n: int, ineqs: List[Dict[str, Any]]) -> List[set[int]]:
    """Compute all 3-subsets of [n] that satisfy all inequalities x(S) <= k."""
    bases: List[set[int]] = []
    for i in range(n):
        for j in range(i + 1, n):
            for k in range(j + 1, n):
                valid = True
                for ineq in ineqs:
                    subset_mask = int(ineq["subset_mask"])
                    k_bound = int(ineq["k"])
                    count = 0
                    if (subset_mask >> i) & 1:
                        count += 1
                    if (subset_mask >> j) & 1:
                        count += 1
                    if (subset_mask >> k) & 1:
                        count += 1
                    if count > k_bound:
                        valid = False
                        break
                if valid:
                    bases.append({i, j, k})
    return bases


def ineqs_to_canonical_string(n: int, ineqs: List[Dict[str, Any]]) -> str:
    """Human-readable string like: [012<=2] [34567<=1]."""
    if not ineqs:
        return ""
    parts: List[str] = []
    for ineq in ineqs:
        subset_mask = int(ineq["subset_mask"])
        k_bound = int(ineq["k"])
        indices = []
        for bit_idx in range(n):
            if (subset_mask >> bit_idx) & 1:
                indices.append(str(bit_idx))
        parts.append(f"[{''.join(indices)}<={k_bound}]")
    return " ".join(parts)


def _normalize_perm_0based(one_line: List[int], n: int) -> List[int]:
    """Normalize a Sage one-line permutation to 0-based [0..n-1]."""
    vals = [int(x) for x in one_line]
    s = set(vals)
    if s == set(range(n)):
        return vals
    if s == set(range(1, n + 1)):
        return [x - 1 for x in vals]
    raise ValueError(f"Unexpected permutation values for n={n}: {sorted(s)[:10]} ...")


def aut_gens_as_0based_lists(G, n: int) -> List[List[int]]:
    """Return a small generating set for Aut(M) as 0-based one-line permutations."""
    gens = list(G.gens())
    out: List[List[int]] = []
    for g in gens:
        # Try g(i) for i=0..n-1
        try:
            img0 = [int(g(i)) for i in range(n)]
            out.append(_normalize_perm_0based(img0, n))
            continue
        except Exception:
            pass
        # Fallback: g(i+1) for i=0..n-1
        try:
            img1 = [int(g(i + 1)) for i in range(n)]
            out.append(_normalize_perm_0based(img1, n))
            continue
        except Exception as e:
            raise ValueError(f"Could not extract generator permutation in a supported convention: {g}") from e

    # Deduplicate generators
    dedup: List[List[int]] = []
    seen: set[Tuple[int, ...]] = set()
    for p in out:
        t = tuple(p)
        if t not in seen:
            seen.add(t)
            dedup.append(p)
    return dedup


def _default_paths_for_n(n: int) -> Tuple[str, str, str]:
    inp = f"data/allr3n{n}.txt"
    txt = f"data/r3n{n}_auto.txt"
    js = f"data/r3n{n}_autinfo.jsonl"
    return inp, txt, js


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("n", nargs="?", type=int, default=None, help="Ground set size n (produces r3n{n}_auto.txt and r3n{n}_autinfo.jsonl)")
    ap.add_argument("--n", dest="n_flag", type=int, default=None, help="Same as positional n")
    ap.add_argument("--input", default=None, help="JSONL orbit-rep file (from sage_generate_allr3_reference.sage)")
    ap.add_argument("--txt-out", default=None, help="Human-readable output (backward compatible)")
    ap.add_argument("--jsonl-out", default=None, help="Machine-readable output (includes generators)")
    ap.add_argument("--max-orbits", type=int, default=0, help="If >0, limit to first N orbit reps (debug)")
    args = ap.parse_args()

    n: Optional[int] = args.n_flag if args.n_flag is not None else args.n
    if n is None:
        n = 8  # historical default

    default_in, default_txt, default_jsonl = _default_paths_for_n(int(n))
    input_path = args.input or default_in
    txt_out = args.txt_out or default_txt
    jsonl_out = args.jsonl_out or default_jsonl

    orders: List[Tuple[int, str, int]] = []
    by_order = Counter()
    jsonl_recs: List[Dict[str, Any]] = []

    with open(input_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            rec = json.loads(line)
            rec_n = int(rec["n"])
            if rec_n != int(n):
                # Allow mixed files, but warn
                # (we still output by actual rec_n if needed).
                pass

            orbit_id = int(rec.get("orbit_id", -1))
            ineqs = rec.get("ineqs", [])

            bases = bases_from_inequalities(rec_n, ineqs)
            if not bases:
                print(f"Warning: orbit {orbit_id} has no valid bases; skipping")
                continue

            # Matroid from bases; check=False can speed up if available in your Sage
            try:
                M = Matroid(groundset=range(rec_n), bases=bases, check=False)
            except TypeError:
                M = Matroid(groundset=range(rec_n), bases=bases)

            G = M.automorphism_group()
            o = int(G.order())
            gens = aut_gens_as_0based_lists(G, rec_n)

            canonical_str = ineqs_to_canonical_string(rec_n, ineqs)
            orders.append((orbit_id, canonical_str, o))
            by_order[o] += 1

            jsonl_recs.append(
                {
                    "orbit_id": orbit_id,
                    "n": rec_n,
                    "aut_order": int(o),
                    "aut_gens": gens,
                }
            )

            if args.max_orbits and len(orders) >= args.max_orbits:
                break

    # Ensure output dirs exist
    for p in [txt_out, jsonl_out]:
        out_dir = os.path.dirname(p)
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)

    with open(txt_out, "w", encoding="utf-8") as out:
        for orbit_id, canonical_str, o in orders:
            if canonical_str:
                out.write(f"Orbit {orbit_id}: {canonical_str} |Aut| = {int(o)}\n")
            else:
                out.write(f"Orbit {orbit_id}: (empty - trivial matroid) |Aut| = {int(o)}\n")

    with open(jsonl_out, "w", encoding="utf-8") as out:
        for r in jsonl_recs:
            out.write(json.dumps(r, sort_keys=True) + "\n")

    print(f"Output saved to {txt_out} and {jsonl_out}")
    print("\nHistogram of |Aut(C)|:")
    for o in sorted(by_order):
        print(f"  {o:>6}: {by_order[o]}")
    print("\nNontrivial examples (first 20):")
    shown = 0
    for orbit_id, canonical_str, o in orders:
        if o != 1:
            print(f"orbit {orbit_id}: {canonical_str} |Aut|={o}")
            shown += 1
            if shown >= 20:
                break


if __name__ == "__main__":
    main()
