"""Statistics for raw_1000.csv (scenario,mode,block,run,ms) from bench_1000.sh.

Usage: python3 -I analyze_1000.py results_board/raw_1000.csv
"""
import csv
import statistics
import sys
from collections import defaultdict


def pct(sorted_vals, p):
    """Percentile with linear interpolation (p in 0..100)."""
    k = (len(sorted_vals) - 1) * p / 100
    lo, hi = int(k), min(int(k) + 1, len(sorted_vals) - 1)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def main(path):
    vals = defaultdict(list)                       # (scenario, mode) -> [ms]
    blocks = defaultdict(lambda: defaultdict(list))  # (scenario, mode) -> block -> [ms]
    with open(path) as f:
        for row in csv.DictReader(f):
            key = (row["scenario"], row["mode"])
            ms = float(row["ms"])
            vals[key].append(ms)
            blocks[key][int(row["block"])].append(ms)

    labels = {
        "hackathon": "Hackathon server (IPv4), TLS handshake time",
        "own": "Own server (LAN), end-to-end time",
    }
    for scenario in ("hackathon", "own"):
        print(f"\n## {labels[scenario]}")
        print(f"{'mode':8} {'n':>5} {'best':>8} {'p5':>8} {'median':>8} {'mean':>8} "
              f"{'p95':>8} {'p99':>8} {'worst':>8} {'stdev':>7}")
        med = {}
        for mode in ("legacy", "hybrid"):
            v = sorted(vals[(scenario, mode)])
            if not v:
                continue
            med[mode] = statistics.median(v)
            print(f"{mode:8} {len(v):5d} {v[0]:8.2f} {pct(v, 5):8.2f} {med[mode]:8.2f} "
                  f"{statistics.mean(v):8.2f} {pct(v, 95):8.2f} {pct(v, 99):8.2f} "
                  f"{v[-1]:8.2f} {statistics.stdev(v):7.2f}")
        if len(med) == 2:
            d = med["hybrid"] - med["legacy"]
            print(f"median difference hybrid - legacy: {d:+.2f} ms ({100 * d / med['legacy']:+.1f}%)")
            lb, hb = blocks[(scenario, "legacy")], blocks[(scenario, "hybrid")]
            common = sorted(set(lb) & set(hb))
            diffs = [statistics.median(hb[b]) - statistics.median(lb[b]) for b in common]
            faster = sum(1 for x in diffs if x < 0)
            print(f"per-block median difference over {len(common)} blocks: "
                  f"min {min(diffs):+.2f} / median {statistics.median(diffs):+.2f} / "
                  f"max {max(diffs):+.2f} ms; hybrid faster in {faster} of {len(common)} blocks")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "results_board/raw_1000.csv")
