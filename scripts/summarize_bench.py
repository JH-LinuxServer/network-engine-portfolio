#!/usr/bin/env python3
import argparse
import glob
import os
import re
import statistics
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

METRIC_KEYS = [
    "Total RTT",
    "Hop1(C->F)",
    "Hop2(F->M)",
    "Hop3(M->F)",
    "Hop4(F->C)",
]

@dataclass
class RunResult:
    scenario: str
    ts_dir: str
    metrics: Dict[str, Dict[str, float]]  # metrics[name][field]=value
    ops_sec: Optional[float] = None
    elapsed_s: Optional[float] = None

def _try_float(s: str) -> Optional[float]:
    try:
        return float(s)
    except Exception:
        return None

def parse_report_txt(path: str) -> Tuple[Dict[str, Dict[str, float]], Optional[float], Optional[float]]:
    """
    Parses:
      - table rows: Metric | Min(ns) | Avg(ns) | Max(ns) | p50 | p99 | p99.9 |
      - lines: Elapsed(s): X
               Ops/sec   : Y
    """
    metrics: Dict[str, Dict[str, float]] = {}
    ops_sec = None
    elapsed_s = None

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")

            # Ops/sec, Elapsed
            if "Ops/sec" in line:
                m = re.search(r"Ops/sec\s*:\s*([0-9]+(?:\.[0-9]+)?)", line)
                if m:
                    ops_sec = float(m.group(1))
                continue
            if "Elapsed(s)" in line:
                m = re.search(r"Elapsed\(s\)\s*:\s*([0-9]+(?:\.[0-9]+)?)", line)
                if m:
                    elapsed_s = float(m.group(1))
                continue

            # Table rows
            if "|" not in line:
                continue

            parts = [p.strip() for p in line.split("|")]
            if len(parts) < 7:
                continue

            name = parts[0]
            if name not in METRIC_KEYS:
                continue

            # parts example:
            # [name, min, avg, max, p50, p99, p99.9, ...maybe empty]
            fields = {
                "min": _try_float(parts[1]),
                "avg": _try_float(parts[2]),
                "max": _try_float(parts[3]),
                "p50": _try_float(parts[4]),
                "p99": _try_float(parts[5]),
                "p999": _try_float(parts[6]),
            }
            # drop Nones
            metrics[name] = {k: v for k, v in fields.items() if v is not None}

    return metrics, ops_sec, elapsed_s

def find_runs(root_dir: str, scenario: str) -> List[str]:
    base = os.path.join(root_dir, "results", scenario)
    if not os.path.isdir(base):
        return []
    # timestamp dirs are like 20260205_123456
    dirs = sorted(
        [d for d in glob.glob(os.path.join(base, "*")) if os.path.isdir(d)],
        reverse=True
    )
    return dirs

def load_runs(root_dir: str, scenario: str, last: int) -> List[RunResult]:
    dirs = find_runs(root_dir, scenario)[:last]
    runs: List[RunResult] = []
    for d in dirs:
        report = os.path.join(d, "report.txt")
        if not os.path.isfile(report):
            # fallback: try loadgen.log if report missing
            lg = os.path.join(d, "loadgen.log")
            if not os.path.isfile(lg):
                continue
            # if someone changed script, still skip for now
            continue

        metrics, ops_sec, elapsed_s = parse_report_txt(report)
        if not metrics:
            continue

        runs.append(RunResult(
            scenario=scenario,
            ts_dir=os.path.basename(d),
            metrics=metrics,
            ops_sec=ops_sec,
            elapsed_s=elapsed_s
        ))
    # reverse to chronological order for printing (old -> new)
    runs.reverse()
    return runs

def median(xs: List[float]) -> Optional[float]:
    xs = [x for x in xs if x is not None]
    if not xs:
        return None
    return statistics.median(xs)

def fmt(v: Optional[float], digits: int = 3) -> str:
    if v is None:
        return "-"
    # ns are integer-like, ops/sec often float
    if abs(v) >= 1000 and float(v).is_integer():
        return str(int(v))
    return f"{v:.{digits}f}"

def summarize(runs: List[RunResult]) -> Dict[str, Dict[str, Optional[float]]]:
    """
    Returns summary for scenario:
      summary["Total RTT"]["p50"]=median(...)
      summary["Ops/sec"]["median"]=median(...) 
    """
    out: Dict[str, Dict[str, Optional[float]]] = {}

    for name in METRIC_KEYS:
        out[name] = {}
        for field in ["p50", "p99", "p999"]:
            vals = []
            for r in runs:
                vals.append(r.metrics.get(name, {}).get(field))
            out[name][field] = median([v for v in vals if v is not None])

    out["__meta__"] = {
        "runs": float(len(runs))
    }
    out["Ops/sec"] = {
        "median": median([r.ops_sec for r in runs if r.ops_sec is not None])
    }
    out["Elapsed(s)"] = {
        "median": median([r.elapsed_s for r in runs if r.elapsed_s is not None])
    }
    return out

def print_runs_table(runs: List[RunResult]):
    if not runs:
        print("No runs found.")
        return

    # Compact: only show Total RTT p50/p99/p999 and ops/sec
    header = ["ts", "RTT_p50", "RTT_p99", "RTT_p999", "Ops/sec", "Elapsed(s)"]
    print("\nPer-run (most useful fields)")
    print(" | ".join(header))
    print("-" * (len(" | ".join(header)) + 10))

    for r in runs:
        total = r.metrics.get("Total RTT", {})
        row = [
            r.ts_dir,
            fmt(total.get("p50")),
            fmt(total.get("p99")),
            fmt(total.get("p999")),
            fmt(r.ops_sec),
            fmt(r.elapsed_s),
        ]
        print(" | ".join(row))

def print_summary(scenario: str, runs: List[RunResult]):
    s = summarize(runs)
    n = int(s["__meta__"]["runs"] or 0)
    print(f"\nSummary (median of {n} runs) - {scenario}")
    print("Metric | p50(ns) | p99(ns) | p99.9(ns)")
    print("--------------------------------------")
    for name in METRIC_KEYS:
        print(
            f"{name} | {fmt(s[name].get('p50'))} | {fmt(s[name].get('p99'))} | {fmt(s[name].get('p999'))}"
        )
    print("--------------------------------------")
    print(f"Ops/sec (median): {fmt(s['Ops/sec'].get('median'))}")
    print(f"Elapsed(s) (median): {fmt(s['Elapsed(s)'].get('median'))}")

def write_csv(path: str, all_runs: Dict[str, List[RunResult]]):
    # One row per run
    import csv
    fields = [
        "scenario", "ts",
        "RTT_p50", "RTT_p99", "RTT_p999",
        "Hop1_p99", "Hop1_p999",
        "Hop2_p99", "Hop2_p999",
        "Hop3_p99", "Hop3_p999",
        "Hop4_p99", "Hop4_p999",
        "ops_sec", "elapsed_s"
    ]
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for scen, runs in all_runs.items():
            for r in runs:
                def g(m, k):
                    return r.metrics.get(m, {}).get(k)
                row = {
                    "scenario": scen,
                    "ts": r.ts_dir,
                    "RTT_p50": g("Total RTT", "p50"),
                    "RTT_p99": g("Total RTT", "p99"),
                    "RTT_p999": g("Total RTT", "p999"),
                    "Hop1_p99": g("Hop1(C->F)", "p99"),
                    "Hop1_p999": g("Hop1(C->F)", "p999"),
                    "Hop2_p99": g("Hop2(F->M)", "p99"),
                    "Hop2_p999": g("Hop2(F->M)", "p999"),
                    "Hop3_p99": g("Hop3(M->F)", "p99"),
                    "Hop3_p999": g("Hop3(M->F)", "p999"),
                    "Hop4_p99": g("Hop4(F->C)", "p99"),
                    "Hop4_p999": g("Hop4(F->C)", "p999"),
                    "ops_sec": r.ops_sec,
                    "elapsed_s": r.elapsed_s,
                }
                w.writerow(row)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".", help="project root (default: .)")
    ap.add_argument("--scenarios", nargs="+", default=["s1", "s2", "s3"])
    ap.add_argument("--last", type=int, default=5, help="how many recent runs per scenario")
    ap.add_argument("--csv", default="", help="write per-run rows to CSV")
    args = ap.parse_args()

    all_runs: Dict[str, List[RunResult]] = {}
    for scen in args.scenarios:
        runs = load_runs(args.root, scen, args.last)
        all_runs[scen] = runs

    for scen in args.scenarios:
        runs = all_runs.get(scen, [])
        print_runs_table(runs)
        print_summary(scen, runs)

    if args.csv:
        write_csv(args.csv, all_runs)
        print(f"\nWrote CSV: {args.csv}")

if __name__ == "__main__":
    main()
