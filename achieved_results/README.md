# Achieved results: hybrid PQC on the StarFive VisionFive 2

Measured results from the FOCAL "Post-Quantum Retrofit" hackathon work: legacy vs hybrid post-quantum TLS 1.3
on a StarFive VisionFive 2 (JH7110, 4x SiFive U74 @ up to 1.5 GHz), OpenSSL 3.5.9.

- **Legacy:** X25519 key exchange (+ RSA-3072 server authentication)
- **Hybrid:** X25519MLKEM768 key exchange (+ ECDSA P-256 and ML-DSA-44 dual-signature authentication on our own server)

Two scenarios are measured throughout:
1. **Hackathon server** (IPv4), TLS handshake time, with server authentication RSA-3072 in both modes
2. **Own server** on the local network, end-to-end time including full hybrid authentication

Start with **[SUMMARY.md](SUMMARY.md)**; sections 10 and 11 cover the results in this folder.

## Headline result

| Scenario | Baseline: legacy, default CPU | Hybrid, default CPU | **Hybrid, min clock 750 MHz** |
|---|---|---|---|
| Hackathon server, handshake (median) | 42.17 ms | 48.30 ms | **40.26 ms** |
| Own server, full hybrid auth, end-to-end (median) | 21.30 ms | 27.84 ms | **16.19 ms** |
| Idle temperature | 44.7 °C | 44.7 °C | **45.2 °C** |

Raising only the minimum CPU clock (`ondemand` governor, `scaling_min_freq` = 750 MHz) lets hybrid PQC beat the
legacy baseline for +0.5 °C. Under identical CPU settings hybrid remains slightly slower than legacy
(+0.5 ms on the LAN, +3–4.5 ms over the internet).

## Files

### `SUMMARY.md`
Full project summary: setup, benchmarks, bottleneck analysis, migration guide, limitations.
Section 10 = baseline at the default CPU clock; section 11 = dynamic CPU frequency, including the recommended setting.
Sections 1–9 refer to earlier raw data that isn't included in this folder.

### `baseline_default_clock/`: BASELINE (default CPU setting: `ondemand`, min 312.5 MHz)
| File | Contents |
|---|---|
| `baseline_REPORT_default_clock.md` | Report: legacy vs hybrid, 1000 runs per mode, both scenarios, best / median / percentiles / worst |
| `baseline_raw_1000.csv` | All 4000 measurements: `scenario,mode,block,run,ms` |
| `baseline_stats_1000.txt` | Computed statistics |
| `baseline_bench_1000.log` | Per-block log: governor, temperature, negotiated group / ML-DSA verification checks |

### `dynamic_cpu_freq/`: DYNAMIC CPU FREQ (raise only the minimum CPU clock)
| File | Contents |
|---|---|
| `dynamic_cpu_freq_REPORT.md` | **Final report**: default vs `ondemand` + min 750 MHz, alternated block by block, 1000 runs per mode; includes the **RECOMMENDATION** section comparing all settings |
| `dynamic_cpu_freq_screen_REPORT.md` | Screening of four settings (ondemand min 750 / 1250 MHz, schedutil min 312.5 / 750 MHz), 500 runs per mode |
| `dynamic_cpu_freq_confirm_raw.csv` | Confirmation run, all 8000 measurements: `config,scenario,mode,block,run,ms` (D0 = default, D1 = min 750 MHz) |
| `dynamic_cpu_freq_confirm_temps.csv` | Confirmation run temperatures: `config,phase,elapsed_s,temp_c` |
| `dynamic_cpu_freq_confirm_stats.txt` | Confirmation run statistics, incl. paired per-block comparison |
| `dynamic_cpu_freq_confirm.log` | Confirmation run per-block log and checks |
| `dynamic_cpu_freq_screen_raw.csv` | Screening run, all 8000 measurements (D1–D4) |
| `dynamic_cpu_freq_screen_temps.csv` | Screening run temperatures (idle + under load, every 5 s) |
| `dynamic_cpu_freq_screen_stats.txt` | Screening run statistics |
| `dynamic_cpu_freq_screen.log` | Screening run per-config log and checks |

## Method notes
- Every individual measurement is recorded; modes (and, in the confirmation run, CPU settings) alternate in
  blocks of 50 so both see the same network conditions.
- Every hybrid block was checked: X25519MLKEM768 negotiated and, on the own server, the ML-DSA-44 signature verified.
- 0 failed connections across all runs in this folder.
- Not measured: energy (temperature used as the proxy); minimum clocks of 500 and 625 MHz.
