# ALL APPROACHES COMBINED vs BASELINE

Date: 2026-11-11, 15:50–17:00 UTC. Board: VisionFive 2 (JH7110, 4x SiFive U74), OpenSSL 3.5.9.
Combines the three approaches tested separately (see their own folders) and compares them with the
**BASELINE** in the same session.

## The two configurations

| | **BASE** (= baseline setup) | **COMBINED** (all three approaches) |
|---|---|---|
| CPU clock | default: `ondemand`, min 312.5 MHz | **approach 1:** `ondemand`, **min 750 MHz** |
| OpenSSL build | plain `rv64gc` (std for legacy, opt for hybrid) | **approach 2:** `-march=rv64gc_zba_zbb` (bitmanip) |
| Hybrid authentication (own server) | v1: ML-DSA-44 after the handshake, one core | **approach 3:** v2: ML-DSA-44 in the TLS Certificate, verified on a **second core** in parallel with ECDSA |
| Legacy | X25519 + RSA-3072 | X25519 + RSA-3072 (same clock and build as COMBINED) |

Against the hackathon server only approaches 1 and 2 apply: the server authenticates with RSA-3072 only, so the
hybrid authentication (and with it approach 3) can't be used there. Hybrid there means X25519MLKEM768 key exchange.

## Method
- 20 blocks; in every block both configurations were measured back to back (order alternating each block), each with
  50 legacy + 50 hybrid connections per scenario, so both saw the same network conditions.
- **1000 measurements per configuration, mode and scenario; 8000 connections, 0 failures.** Every hybrid block was
  checked: X25519MLKEM768 negotiated in all 40 hackathon-server hybrid blocks, ML-DSA-44 verified in all 20 own-server
  hybrid blocks of each configuration.
- Idle temperature of both configurations measured back to back (120 s idle each) before the run; temperature
  recorded at the start of every block. The CPU setting was restored to the default automatically at the end.
- Files: `combined_raw.csv` (`config,scenario,mode,block,run,ms`), `combined_temps.csv`, `combined_stats.txt`,
  `combined.log`, `scripts/bench_combined.sh`.

## Results

### Own server (LAN), end-to-end time, full hybrid authentication

| Config | Mode | Best | **Median** | Mean | p95 | Worst |
|---|---|---|---|---|---|---|
| BASE | Legacy (X25519 + RSA) | 12.64 | **21.04** | 21.17 | 24.19 | 35.08 ms |
| BASE | Hybrid (ML-KEM + ECDSA + ML-DSA-44, v1) | 20.40 | **28.06** | 28.25 | 31.80 | 107.13 ms |
| COMBINED | Legacy (X25519 + RSA) | 8.96 | **15.35** | 15.39 | 17.79 | 21.39 ms |
| **COMBINED** | **Hybrid (ML-KEM + ECDSA + ML-DSA-44, v2 two cores)** | **12.21** | **14.47** | **14.74** | **17.41** | **26.57 ms** |

### Hackathon server (IPv4), TLS handshake time

| Config | Mode | Best | **Median** | Mean | p95 | Worst |
|---|---|---|---|---|---|---|
| BASE | Legacy (X25519) | 28.19 | **38.36** | 42.59 | 60.48 | 354.25 ms |
| BASE | Hybrid (X25519MLKEM768) | 31.40 | **43.76** | 48.15 | 71.96 | 356.88 ms |
| COMBINED | Legacy (X25519) | 25.90 | **32.99** | 36.76 | 55.41 | 355.01 ms |
| **COMBINED** | **Hybrid (X25519MLKEM768)** | **27.73** | **35.62** | **39.33** | **58.86** | **140.84 ms** |

### Target check: COMBINED hybrid vs the legacy BASELINE (paired per block)

| Scenario | Baseline: legacy (BASE) | **Hybrid (COMBINED)** | Change | Hybrid faster in |
|---|---|---|---|---|
| **Own server, full hybrid auth, end-to-end** | 21.04 ms | **14.47 ms** | **−6.57 ms (−31.2 %)** | **20/20 blocks** |
| **Hackathon server, handshake** | 38.36 ms | **35.62 ms** | **−2.73 ms (−7.1 %)** | **17/20 blocks** |

### What COMBINED changes for each mode (paired per block)

| Scenario | Mode | BASE → COMBINED | Faster in |
|---|---|---|---|
| Own server | Hybrid | 28.06 → 14.47 ms (**−13.59 ms, −48.4 %**) | 20/20 |
| Own server | Legacy | 21.04 → 15.35 ms (−5.70 ms, −27.1 %) | 20/20 |
| Hackathon server | Hybrid | 43.76 → 35.62 ms (**−8.14 ms, −18.6 %**) | 20/20 |
| Hackathon server | Legacy | 38.36 → 32.99 ms (−5.36 ms, −14.0 %) | 18/20 |

Hybrid − legacy within the same configuration: BASE +7.15 ms (own) / +5.58 ms (hackathon); COMBINED **−0.99 ms
(own, hybrid faster in 20/20 blocks)** / +2.84 ms (hackathon).

### Temperature (°C)

| Config | Idle after 120 s | During the run (start of each block) mean / max |
|---|---|---|
| BASE | 45.9 | 47.8 / 48.9 |
| COMBINED | 47.2 | 47.8 / 48.5 |

For reference: full CPU speed (`performance`, fixed 1.5 GHz) idled at ~50–52 °C (2026-11-10).

## How the approaches add up (own server, full hybrid, end-to-end median)

| Configuration | Hybrid median | Source |
|---|---|---|
| Baseline (default clock, plain build, v1, one core) | 27.95 ms (28.06 ms in this run) | `baseline_REPORT_default_clock.md`, this run |
| Approach 2 only: bitmanip build | 26.57 ms | `bitmanip_REPORT.md` |
| Approach 3 only: ML-DSA on a second core (v2) | 24.57 ms | `multicore_e2e_REPORT.md` |
| Approach 1 only: min 750 MHz | 16.19 ms | `dynamic_cpu_freq_REPORT.md` |
| **All three combined** | **14.47 ms** | this report |
| *Legacy baseline, for comparison* | *21.04–21.68 ms* | all runs |

The single-approach rows come from different runs (each with its own alternating comparison), so compare them
as an indication; the clean comparison is BASE vs COMBINED in this run. The approaches don't add up linearly:
raising the minimum clock removes most of the slowdown, and the other two then shorten the remaining crypto work.

## Findings

1. **The target is met with margin.** With all three approaches, full hybrid post-quantum TLS (ML-KEM-768 key exchange
   + ECDSA/ML-DSA-44 dual-signature authentication) is **31 % faster than the legacy baseline** on the own server
   (14.47 vs 21.04 ms, 20/20 blocks), and the hybrid key exchange against the hackathon server is **7 % faster than
   the legacy baseline** (35.62 vs 38.36 ms, 17/20 blocks).
2. **Under identical settings hybrid is no slower than legacy on the own server:** with COMBINED applied to both,
   hybrid is 0.99 ms faster than legacy (20/20 blocks), mostly because the server's RSA-3072 signing is slower than
   its ECDSA + ML-DSA-44 signing. Against the internet server the larger ML-KEM messages still cost +2.8 ms under
   identical settings.
3. **Hybrid gains twice as much as legacy from the same changes** (−13.6 vs −5.7 ms on the own server), because hybrid
   has more client-side crypto, which is what the three approaches speed up.
4. **Small heat cost:** +1.3 °C at idle (47.2 vs 45.9 °C); no difference during the run (both 47.8 °C mean).
   Full speed would cost ~+5 °C.
5. **Software and settings only:** no hardware accelerator, no new chip. The changes are a CPU clock setting, a
   compiler flag and a different place/core for the ML-DSA check.

## Limits
- Hybrid authentication was measured against our own server; the hackathon server only supports RSA-3072.
- The v2 ML-DSA signature uses a private-use TLS extension (`0xFE20`), not a standard; see `multicore_e2e_REPORT.md`.
- The bitmanip build runs only on CPUs with Zba/Zbb.
- The key exchange halves (X25519 ‖ ML-KEM) are still computed one after another inside OpenSSL (possible further
  gain of ~0.5–1 ms at 750 MHz, see `multicore_REPORT.md`).
- Energy wasn't measured; temperature is the proxy.
- The 750 MHz minimum isn't made permanent on the board; it was active only during the measurements.
