"""Compare dynamic-CPU-frequency configs with the baseline (default ondemand).

Usage: python3 -I analyze_dyn.py BASELINE_RAW DYN_RAW DYN_TEMPS
  BASELINE_RAW  scenario,mode,block,run,ms          (baseline_raw_1000.csv)
  DYN_RAW       config,scenario,mode,block,run,ms   (dynamic_cpu_freq_*_raw.csv)
  DYN_TEMPS     config,phase,elapsed_s,temp_c       (dynamic_cpu_freq_*_temps.csv)
"""
import csv
import statistics
import sys
from collections import defaultdict

CONFIGS = {
    "BASE": "baseline: ondemand, min 312.5 MHz (default)",
    "D0": "default again, alternated with D1",
    "D1": "ondemand, min 750 MHz",
    "D2": "ondemand, min 1250 MHz",
    "D3": "schedutil, min 312.5 MHz",
    "D4": "schedutil, min 750 MHz",
}
SCENARIOS = {
    "hackathon": "Hackathon server (IPv4), TLS handshake",
    "own": "Own server (LAN), end-to-end",
}


def pct(v, p):
    k = (len(v) - 1) * p / 100
    lo, hi = int(k), min(int(k) + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def load(base_path, dyn_path):
    vals = defaultdict(list)          # (config, scenario, mode) -> [ms]
    with open(base_path) as f:
        for r in csv.DictReader(f):
            vals[("BASE", r["scenario"], r["mode"])].append(float(r["ms"]))
    with open(dyn_path) as f:
        for r in csv.DictReader(f):
            vals[(r["config"], r["scenario"], r["mode"])].append(float(r["ms"]))
    return vals


def temps(path):
    idle_end, load_t = {}, defaultdict(list)
    with open(path) as f:
        for r in csv.DictReader(f):
            t = float(r["temp_c"])
            if r["phase"] == "idle":
                idle_end[r["config"]] = t          # last idle sample = settled idle temp
            else:
                load_t[r["config"]].append(t)
    return idle_end, load_t


def main(base_path, dyn_path, temp_path):
    vals = load(base_path, dyn_path)
    idle_end, load_t = temps(temp_path)
    configs = [c for c in CONFIGS if any(k[0] == c for k in vals)]

    for s, title in SCENARIOS.items():
        print(f"\n## {title}")
        print(f"{'config':6} {'mode':7} {'n':>5} {'best':>8} {'median':>8} {'p95':>8} {'worst':>8}"
              f" {'hyb-leg':>9} {'vs BASE':>9}")
        base_med = {m: statistics.median(vals[("BASE", s, m)]) for m in ("legacy", "hybrid")}
        for c in configs:
            med = {}
            for m in ("legacy", "hybrid"):
                v = sorted(vals[(c, s, m)])
                med[m] = statistics.median(v)
                extra = ""
                if m == "hybrid":
                    d = med["hybrid"] - med["legacy"]
                    extra = f" {d:+8.2f}"
                else:
                    extra = " " * 9
                vs = med[m] - base_med[m]
                print(f"{c:6} {m:7} {len(v):5d} {v[0]:8.2f} {med[m]:8.2f} {pct(v, 95):8.2f} "
                      f"{v[-1]:8.2f}{extra} {vs:+9.2f}")

    print("\n## Temperature (deg C)")
    print(f"{'config':6} {'setting':44} {'idle (after 120 s)':>18} {'load mean':>10} {'load max':>9}")
    for c in configs:
        if c == "BASE":
            print(f"{c:6} {CONFIGS[c]:44} {'~45.4*':>18} {'~45.8*':>10} {'45.9*':>9}")
            continue
        lt = load_t.get(c, [])
        print(f"{c:6} {CONFIGS[c]:44} {idle_end.get(c, float('nan')):18.1f} "
              f"{statistics.mean(lt) if lt else float('nan'):10.1f} {max(lt) if lt else float('nan'):9.1f}")
    print("* baseline temperatures from the start/end readings in baseline_bench_1000.log and idle "
          "readings that day (no 5 s sampling during the baseline run)")


if __name__ == "__main__":
    main(*sys.argv[1:4])
