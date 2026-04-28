#!/usr/bin/env python3
"""Merge JSONL shard outputs by canonical_key.

Usage:
    python3 scripts/merge_jsonl_by_key.py merged.jsonl shard0.jsonl shard1.jsonl ...

The canonical augmentation root shards should already be disjoint.  This script is a
final safety deduplicator and produces deterministic output sorted by canonical_key.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

def main() -> int:
    if len(sys.argv) < 3:
        print("usage: merge_jsonl_by_key.py OUT.jsonl IN1.jsonl [IN2.jsonl ...]", file=sys.stderr)
        return 2

    out_path = Path(sys.argv[1])
    rows: dict[str, str] = {}
    duplicates = 0

    for name in sys.argv[2:]:
        path = Path(name)
        with path.open("r", encoding="utf-8") as fh:
            for line_no, line in enumerate(fh, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError as e:
                    raise SystemExit(f"{path}:{line_no}: invalid JSON: {e}") from e
                key = obj.get("canonical_key")
                if not isinstance(key, str):
                    raise SystemExit(f"{path}:{line_no}: missing string canonical_key")
                if key in rows:
                    duplicates += 1
                    continue
                rows[key] = line

    with out_path.open("w", encoding="utf-8") as out:
        for key in sorted(rows):
            out.write(rows[key])
            out.write("\n")

    print(f"merged={len(rows)} duplicates_skipped={duplicates}", file=sys.stderr)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
