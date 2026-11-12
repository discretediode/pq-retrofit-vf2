"""Statistics for parverify (multicore feasibility) results.

Usage: python3 -I analyze_multicore.py multicore_feasibility_raw.csv
CSV: label,pair,mode,iter,us,cpu_classical,cpu_pq   (label = <lib>_<clock>)
"""
import csv
import statistics as st
import sys
from collections import defaultdict

LIBS = ("opt", "bitmanip")
CLOCKS = ("default", "min750", "fixed1500")
PAIRS = ("auth", "kex_keygen", "kex_decaps")
MODES = ("seq", "par_create", "par_pool")


def pct(v, p):
    k = (len(v) - 1) * p / 100
    lo, hi = int(k), min(int(k) + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def main(path):
    vals = defaultdict(list)                 # (label, pair, mode) -> [us]
    by_iter = defaultdict(dict)              # (label, pair, iter) -> {mode: us}
    cores = defaultdict(lambda: [0, 0])      # (label, mode) -> [separate cores, total]
    for r in csv.DictReader(open(path)):
        us = float(r["us"])
        vals[(r["label"], r["pair"], r["mode"])].append(us)
        by_iter[(r["label"], r["pair"], int(r["iter"]))][r["mode"]] = us
        c = cores[(r["label"], r["mode"])]
        c[0] += r["cpu_classical"] != r["cpu_pq"]
        c[1] += 1

    print("## Per configuration: time for both halves of one pair (ms)")
    print(f"{'lib':9} {'clock':10} {'pair':11} {'mode':11} {'n':>4} {'best':>7} {'median':>7} {'p95':>7} "
          f"{'worst':>7} {'saved':>7} {'speedup':>7} {'par faster':>10}")
    summary = {}
    for lib in LIBS:
        for clk in CLOCKS:
            label = f"{lib}_{clk}"
            for pair in PAIRS:
                seq_med = st.median(vals[(label, pair, "seq")])
                for mode in MODES:
                    v = sorted(vals[(label, pair, mode)])
                    if not v:
                        continue
                    med = st.median(v)
                    saved = seq_med - med
                    if mode == "seq":
                        extra = f"{'':>7} {'':>7} {'':>10}"
                    else:
                        wins = sum(1 for it in range(1, 10000)
                                   if (label, pair, it) in by_iter
                                   and by_iter[(label, pair, it)][mode] < by_iter[(label, pair, it)]["seq"])
                        n_it = sum(1 for k in by_iter if k[0] == label and k[1] == pair)
                        extra = f"{saved / 1000:7.3f} {seq_med / med:6.2f}x {wins:4d}/{n_it:<4d}"
                    summary[(lib, clk, pair, mode)] = med
                    print(f"{lib:9} {clk:10} {pair:11} {mode:11} {len(v):4d} {v[0] / 1000:7.3f} {med / 1000:7.3f} "
                          f"{pct(v, 95) / 1000:7.3f} {v[-1] / 1000:7.3f} {extra}")
            print()

    print("## Whole client-side crypto of one hybrid handshake (sum of the three pair medians, ms)")
    print(f"{'lib':9} {'clock':10} {'seq':>7} {'par_create':>10} {'par_pool':>9} {'saved (pool)':>12} {'speedup':>7}")
    for lib in LIBS:
        for clk in CLOCKS:
            tot = {m: sum(summary[(lib, clk, p, m)] for p in PAIRS) / 1000 for m in MODES}
            print(f"{lib:9} {clk:10} {tot['seq']:7.3f} {tot['par_create']:10.3f} {tot['par_pool']:9.3f} "
                  f"{tot['seq'] - tot['par_pool']:12.3f} {tot['seq'] / tot['par_pool']:6.2f}x")

    print("\n## Core usage check: share of runs where the two halves ran on different cores")
    for lib in LIBS:
        for clk in CLOCKS:
            label = f"{lib}_{clk}"
            s = "  ".join(f"{m} {cores[(label, m)][0]}/{cores[(label, m)][1]}" for m in MODES)
            print(f"{label:20} {s}")


if __name__ == "__main__":
    main(sys.argv[1])
