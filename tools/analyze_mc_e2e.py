"""Statistics for the multicore end-to-end benchmark (bench_multicore_e2e.sh).

Usage: python3 -I analyze_mc_e2e.py multicore_e2e_raw.csv [baseline_raw_1000.csv]
CSV: mode,block,run,tcp_ms,handshake_ms,e2e_ms,pq_verify_ms,wait_ms
"""
import csv
import statistics as st
import sys
from collections import defaultdict

MODES = {
    "legacy": "Legacy: X25519 + RSA-3072",
    "v1": "Hybrid v1: ML-DSA after the handshake (one core)",
    "v2_seq": "Hybrid v2: ML-DSA in Certificate, same core",
    "v2_par": "Hybrid v2: ML-DSA in Certificate, 2nd core in parallel",
}


def pct(v, p):
    k = (len(v) - 1) * p / 100
    lo, hi = int(k), min(int(k) + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def main(path, base=None):
    col = defaultdict(lambda: defaultdict(list))      # mode -> field -> [values]
    blk = defaultdict(lambda: defaultdict(list))      # mode -> block -> [e2e]
    for r in csv.DictReader(open(path)):
        for f in ("tcp_ms", "handshake_ms", "e2e_ms", "pq_verify_ms", "wait_ms"):
            col[r["mode"]][f].append(float(r[f]))
        blk[r["mode"]][int(r["block"])].append(float(r["e2e_ms"]))

    print("## End-to-end time per connection (ms), own server on the LAN")
    print(f"{'mode':7} {'n':>5} {'best':>7} {'p5':>7} {'median':>7} {'mean':>7} {'p95':>7} {'p99':>7} {'worst':>7}")
    for m in MODES:
        v = sorted(col[m]["e2e_ms"])
        print(f"{m:7} {len(v):5d} {v[0]:7.2f} {pct(v, 5):7.2f} {st.median(v):7.2f} {st.mean(v):7.2f} "
              f"{pct(v, 95):7.2f} {pct(v, 99):7.2f} {v[-1]:7.2f}")

    print("\n## Breakdown, medians (ms)")
    print(f"{'mode':7} {'TCP':>7} {'TLS handshake':>14} {'ML-DSA verify':>14} {'wait after TLS':>15}")
    for m in MODES:
        c = col[m]
        print(f"{m:7} {st.median(c['tcp_ms']):7.2f} {st.median(c['handshake_ms']):14.2f} "
              f"{st.median(c['pq_verify_ms']):14.2f} {st.median(c['wait_ms']):15.3f}")

    print("\n## Paired per-block comparison of the median end-to-end time (same block = same conditions)")
    for a, b in (("v1", "v2_seq"), ("v2_seq", "v2_par"), ("v1", "v2_par"),
                 ("legacy", "v1"), ("legacy", "v2_seq"), ("legacy", "v2_par")):
        common = sorted(set(blk[a]) & set(blk[b]))
        d = [st.median(blk[b][x]) - st.median(blk[a][x]) for x in common]
        ma, mb = st.median(col[a]["e2e_ms"]), st.median(col[b]["e2e_ms"])
        print(f"{b:7} vs {a:7}: overall {mb - ma:+6.2f} ms ({100 * (mb - ma) / ma:+5.1f} %); per block median "
              f"{st.median(d):+6.2f} ms, range {min(d):+6.2f}..{max(d):+6.2f}; {b} faster in "
              f"{sum(x < 0 for x in d)}/{len(d)} blocks")

    if base:
        bv = defaultdict(list)
        for r in csv.DictReader(open(base)):
            if r["scenario"] == "own":
                bv[r["mode"]].append(float(r["ms"]))
        bl = st.median(bv["legacy"])
        print(f"\n## Against the BASELINE run (own server, default clock): legacy {bl:.2f} ms, "
              f"hybrid {st.median(bv['hybrid']):.2f} ms")
        for m in MODES:
            mm = st.median(col[m]["e2e_ms"])
            print(f"{m:7} median {mm:6.2f} ms -> {mm - bl:+6.2f} ms vs baseline legacy")


if __name__ == "__main__":
    main(*sys.argv[1:3])
