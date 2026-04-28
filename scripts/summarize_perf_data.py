#!/usr/bin/env python3
"""Summarize alexeev_r3_cpp perf_data.txt JSONL records.

Dependency-free helper for n=8 production tuning and n=9 wide-engine tuning.
It ranks safe production profiles, flags known anti-patterns, and prints a
manual command matching the best record.

n=8 records use the measured-fast uint64_t engine.
n=9 records have "engine":"n9-wide" and use the isolated Mask128 wide engine.
"""
import json, sys, statistics, collections

path = sys.argv[1] if len(sys.argv) > 1 else "perf_data.txt"
records = []
with open(path, "r", encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            pass

if not records:
    print("No JSON records found.")
    raise SystemExit(1)

def is_n9(r):
    return int(r.get("n", 8)) == 9 or r.get("engine") == "n9-wide"

def safe_prod(r):
    name = r.get("name","")
    if r.get("parallel_depth",1) != 1:
        return False
    if not is_n9(r) and r.get("scheduler") == "work-steal":
        return False
    if r.get("labeled_state_cache_max",0) != 0:
        return False

    scope = r.get("state_cache_scope","global")
    if is_n9(r):
        if scope not in ("labeled-global", "global"):
            return False
    else:
        if scope not in ("global",):
            return False

    if r.get("affinity","none") in ("compact", "spread"):
        return False
    if r.get("bitset_kernel","auto") in ("unrolled", "avx2", "avx512"):
        return False
    bad = ("depth2", "work_steal", "prefilter", "canonical", "thread-local",
           "compact_affinity_reference", "spread_affinity_reference")
    return not any(b in name for b in bad)

def key_sps(r): return r.get("solutions_per_sec", 0.0)
def key_states(r): return r.get("states_per_sec", 0.0)
def fmt(r):
    return (f"{r.get('solutions_per_sec',0):10.3f} sol/s  "
            f"{r.get('states_per_sec',0):10.1f} st/s  "
            f"{r.get('elapsed_sec',0):9.3f}s  "
            f"{r.get('solutions',0):7} sol  {r.get('states',0):8} states  "
            f"{r.get('threads','?'):>4} thr  {r.get('scheduler','?'):<13} "
            f"{r.get('compat_cache_policy','?'):<11} "
            f"aff={r.get('affinity','none'):<7} "
            f"bit={r.get('bitset_kernel','auto'):<8} "
            f"key={r.get('state_key_format','binary'):<6} "
            f"wc={r.get('worker_compat_cache_size',0):<4} "
            f"prewarm={r.get('prewarm_hot_compat_limit',0):<5} "
            f"engine={r.get('engine','n8'):<8} "
            f"{r.get('name','?')}")

n_values = sorted({int(r.get("n", 8)) for r in records})
print(f"records: {len(records)}")
print(f"n values present: {n_values}")

# Use separate reliability thresholds for n=8 and n=9.  n=9 is often gathered
# with state-bounded runs before early-solution behavior is known.
solution_reliable = [
    r for r in records
    if safe_prod(r) and r.get("solutions_per_sec",0) > 0
    and ((is_n9(r) and r.get("solutions",0) >= 1) or ((not is_n9(r)) and r.get("solutions",0) >= 5000))
]
state_reliable = [
    r for r in records
    if safe_prod(r) and r.get("states_per_sec",0) > 0
    and ((is_n9(r) and r.get("states",0) >= 1000) or ((not is_n9(r)) and r.get("states",0) >= 50000))
]

print(f"safe production solution records: {len(solution_reliable)}")
print(f"safe production state records: {len(state_reliable)}")

for n in n_values:
    recs = [r for r in solution_reliable if int(r.get("n",8)) == n]
    if recs:
        print(f"\nTop safe n={n} profiles by solutions/sec:")
        for r in sorted(recs, key=key_sps, reverse=True)[:20]:
            print(fmt(r))
    recs_state = [r for r in state_reliable if int(r.get("n",8)) == n]
    if recs_state:
        print(f"\nTop safe n={n} profiles by states/sec:")
        for r in sorted(recs_state, key=key_states, reverse=True)[:20]:
            print(fmt(r))

print("\nReference / anti-pattern diagnostics:")
for r in records:
    name = r.get("name","")
    if ("depth2" in name or "work_steal" in name or "prefilter" in name or
        r.get("parallel_depth",1) != 1 or
        (not is_n9(r) and r.get("state_cache_scope","global") != "global") or
        (is_n9(r) and r.get("state_cache_scope","labeled-global") not in ("labeled-global","global"))):
        print(fmt(r))

def best_for_n(n):
    sols = [r for r in solution_reliable if int(r.get("n",8)) == n]
    states = [r for r in state_reliable if int(r.get("n",8)) == n]
    if sols:
        return max(sols, key=key_sps), "solutions/sec"
    if states:
        return max(states, key=key_states), "states/sec"
    return None, ""

for n in n_values:
    best, metric = best_for_n(n)
    if not best:
        continue
    print(f"\nSuggested learned production profile for n={n} by {metric}:")
    print(fmt(best))
    out_file = f"r3n{n}_tilings.txt"
    cmd = [
        "./alexeev_r3_cpp", str(n),
        "--auto",
        "--threads", str(best.get("threads", 1)),
        "--scheduler", ("static" if is_n9(best) else best.get("scheduler","static")),
        "--parallel-depth", str(best.get("parallel_depth",1)),
        "--compat-cache-policy", ("lru" if is_n9(best) else best.get("compat_cache_policy","lru")),
        "--compat-mem-budget-mb", str(int(best.get("compat_mem_budget_mb", 65536) or 65536)),
        "--state-cache-mb", str(int(best.get("state_cache_mb", 65536) or 65536)),
        "--duplicate-local-cache-max", str(best.get("duplicate_local_cache_max",65536)),
        "--split-max-depth", str(best.get("split_max_depth",4)),
        "--split-min-candidates", str(best.get("split_min_candidates",16)),
        "--affinity", best.get("affinity","none"),
        "--bitset-kernel", best.get("bitset_kernel","auto"),
        "--state-key-format", best.get("state_key_format","binary"),
    ]
    if not is_n9(best):
        cmd += ["--worker-compat-cache-size", str(best.get("worker_compat_cache_size",0))]
    if int(best.get("prewarm_hot_compat_limit", 0) or 0) > 0:
        cmd += ["--prewarm-hot-compat-limit", str(best.get("prewarm_hot_compat_limit", 0))]
        p = best.get("prewarm_hot_compat_rows", "")
        if p:
            cmd += ["--prewarm-hot-compat-rows", p]
    cmd += ["--out", out_file]
    print("\nEquivalent manual command:")
    print(" ".join(cmd))
    print("\nOr let the program pick this profile from the data:")
    print(f"./alexeev_r3_cpp {n} --auto --auto-profile learned --auto-learn {path} --out {out_file}")

def group_report(field, recs, label):
    vals = collections.defaultdict(list)
    for r in recs:
        vals[r.get(field,"?")].append(r.get("solutions_per_sec",0) or r.get("states_per_sec",0))
    if vals:
        print(f"\n{label}: {field} averages over reliable records:")
        for k,v in sorted(vals.items()):
            print(f"  {str(k):<14} mean={statistics.mean(v):.3f} max={max(v):.3f} n={len(v)}")

for n in n_values:
    recs = [r for r in solution_reliable if int(r.get("n",8)) == n]
    if not recs:
        recs = [r for r in state_reliable if int(r.get("n",8)) == n]
    for field in ["scheduler", "compat_cache_policy", "affinity", "bitset_kernel", "state_key_format", "worker_compat_cache_size", "prewarm_hot_compat_limit"]:
        group_report(field, recs, f"n={n}")

print("\nBuilt-in n=8 auto profile guidance:")
print("  throughput : threads≈160 on 96c/192t, scheduler=static-hybrid, compat=lru, worker-cache=64, hot-prewarm=512 if available, affinity=none")
print("  balanced   : threads≈144 on 96c/192t, scheduler=static-hybrid, compat=lru, worker-cache=0,  affinity=none")
print("  interactive: threads≈96  on 96c/192t, scheduler=static-hybrid, compat=lru, worker-cache=0,  affinity=numa when available")
print("\nRecommended n=8 gather run:")
print("./alexeev_r3_cpp 8 --auto --gather-data --gather-matrix full --gather-cache-mode warm-profiled --gather-warm-solutions 10000 --gather-solutions 10000")
print("\nBuilt-in n=9 auto profile guidance from first wide-engine data:")
print("  throughput/balanced: threads≈128 on 96c/192t, root-static wide engine, compat=64GiB, state=64GiB, force propagation on")
print("  interactive        : threads≈96  on 96c/192t, root-static wide engine, compat=64GiB, state=64GiB, force propagation on")
print("  memory-saver       : threads≈48  on 96c/192t, smaller state cache; use only when RAM is constrained")
print("\nRecommended n=9 first gather run:")
print("./alexeev_r3_cpp 9 --auto --gather-data --gather-matrix core --gather-states 100000 --gather-solutions 0")
print("\nRecommended n=9 focused full gather run:")
print("./alexeev_r3_cpp 9 --auto --gather-data --gather-matrix full --gather-states 100000 --gather-solutions 0")
