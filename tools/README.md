# Analysis tools

| File | Used for |
|---|---|
| `analyze_1000.py` | Baseline statistics ([`../results/01_baseline_default_clock/`](../results/01_baseline_default_clock/)) |
| `analyze_dyn.py` | Dynamic CPU frequency runs ([`../results/02_dynamic_cpu_freq/`](../results/02_dynamic_cpu_freq/)) |
| `analyze_pair.py` | Paired two-configuration runs: bitmanip and combined ([`03`](../results/03_bitmanip_zba_zbb/), [`05`](../results/05_combined_all_approaches/)) |
| `analyze_multicore.py` | Multicore feasibility test ([`../results/04_multicore/`](../results/04_multicore/)) |
| `analyze_mc_e2e.py` | Multicore end-to-end TLS runs ([`../results/04_multicore/`](../results/04_multicore/)) |
| `md2pdf.py` | Converts the Markdown reports to PDF (Python-Markdown + headless Chrome) |

The benchmark shell scripts that ran on the board are kept next to their data, in each results folder's `scripts/`.
