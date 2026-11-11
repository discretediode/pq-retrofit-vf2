"""Paired comparison of two configurations measured in alternating blocks.

Usage: python3 -I analyze_pair.py RAW_CSV CONFIG_A CONFIG_B [BASELINE_RAW]
  RAW_CSV       config,scenario,mode,block,run,ms
  BASELINE_RAW  optional scenario,mode,block,run,ms (baseline_raw_1000.csv), shown for reference
"""
import csv
import statistics as st
import sys
from collections import defaultdict

SCENARIOS = {
    "hackathon": "Hackathon server (IPv4), TLS handshake",
    "own": "Own server (LAN), end-to-end",
}


def pct(v, p):
    k = (len(v) - 1) * p / 100
    lo, hi = int(k), min(int(k) + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def main(raw, a, b, base=None):
    allv = defaultdict(list)
    blk = defaultdict(lambda: defaultdict(list))
    for r in csv.DictReader(open(raw)):
        k = (r["config"], r["scenario"], r["mode"])
        allv[k].append(float(r["ms"]))
        blk[k][int(r["block"])].append(float(r["ms"]))
    if base:
        for r in csv.DictReader(open(base)):
            allv[("BASELINE", r["scenario"], r["mode"])].append(float(r["ms"]))

    for s, title in SCENARIOS.items():
        print(f"\n## {title}")
        print(f"{'config':10} {'mode':7} {'n':>5} {'best':>8} {'median':>8} {'mean':>8} {'p95':>8} {'worst':>8}")
        for c in ([ "BASELINE"] if base else []) + [a, b]:
            for m in ("legacy", "hybrid"):
                v = sorted(allv.get((c, s, m), []))
                if v:
                    print(f"{c:10} {m:7} {len(v):5d} {v[0]:8.2f} {st.median(v):8.2f} {st.mean(v):8.2f} "
                          f"{pct(v, 95):8.2f} {v[-1]:8.2f}")
        for m in ("legacy", "hybrid"):
            ma, mb = st.median(allv[(a, s, m)]), st.median(allv[(b, s, m)])
            common = sorted(set(blk[(a, s, m)]) & set(blk[(b, s, m)]))
            d = [st.median(blk[(b, s, m)][x]) - st.median(blk[(a, s, m)][x]) for x in common]
            print(f"{m:7} {b} vs {a}: overall median {mb - ma:+.2f} ms ({100 * (mb - ma) / ma:+.1f}%); "
                  f"per block median {st.median(d):+.2f} ms, range {min(d):+.2f}..{max(d):+.2f}, "
                  f"{b} faster in {sum(x < 0 for x in d)}/{len(d)} blocks")
        for c in (a, b):
            common = sorted(set(blk[(c, s, 'legacy')]) & set(blk[(c, s, 'hybrid')]))
            d = [st.median(blk[(c, s, 'hybrid')][x]) - st.median(blk[(c, s, 'legacy')][x]) for x in common]
            print(f"{c:10} hybrid - legacy: per block median {st.median(d):+.2f} ms, "
                  f"hybrid faster in {sum(x < 0 for x in d)}/{len(d)} blocks")


if __name__ == "__main__":
    main(*sys.argv[1:5])
