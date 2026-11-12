# DYNAMIC CPU FREQ, part 1: screening of four middle-ground CPU settings

Date: 2026-11-11, 09:57–10:53 UTC. Compared with the BASELINE (`baseline_REPORT.md`:
ondemand, min 312.5 MHz, 1000 runs per mode).
Idea tested: keep a power-saving governor, but stop the CPU from starting a TLS handshake at the lowest
clock, by raising `scaling_min_freq` and/or using `schedutil`.

| Config | Governor | Minimum CPU clock |
|---|---|---|
| BASE | ondemand | 312.5 MHz (default) |
| D1 | ondemand | 750 MHz |
| D2 | ondemand | 1250 MHz |
| D3 | schedutil | 312.5 MHz |
| D4 | schedutil | 750 MHz |

Method: per config 120 s idle (temperature every 5 s), then 10 blocks x 50 runs per mode, legacy/hybrid order
alternating per block, both scenarios; temperature sampled every 5 s under load. Configs ran one after
another. 8000 connections, 0 failures; every hybrid block was verified (X25519MLKEM768 negotiated, ML-DSA-44 verified).
The CPU setting was restored to the default automatically at the end.
Files: `dynamic_cpu_freq_screen_raw.csv`, `dynamic_cpu_freq_screen_temps.csv`, `dynamic_cpu_freq_screen.log`,
`dynamic_cpu_freq_screen_stats.txt`.

## Own server (LAN), end-to-end: the clean measurement of the CPU effect

| Config | Legacy median | Hybrid median | Hybrid − legacy | Hybrid vs BASE hybrid | Hybrid best / p95 / worst |
|---|---|---|---|---|---|
| BASE | 21.61 ms | 27.95 ms | +6.35 ms | | 20.51 / 31.58 / 40.05 |
| **D1** | 17.74 ms | **16.59 ms** | **−1.14 ms** | **−11.36 ms (−41 %)** | 14.22 / 19.60 / 24.04 |
| D2 | 17.54 ms | 16.67 ms | −0.87 ms | −11.28 ms | 14.23 / 19.99 / 23.70 |
| D3 | 23.90 ms | 33.85 ms | +9.95 ms | +5.90 ms (worse) | 14.60 / 37.51 / 44.66 |
| D4 | 17.58 ms | 16.54 ms | −1.04 ms | −11.41 ms | 14.12 / 19.76 / 27.43 |

## Hackathon server (IPv4), TLS handshake: confounded by network drift

| Config | Legacy median | Hybrid median | Hybrid − legacy |
|---|---|---|---|
| BASE | 39.92 ms | 45.08 ms | +5.16 ms |
| D1 | 62.28 ms | 67.85 ms | +5.58 ms |
| D2 | 56.31 ms | 54.30 ms | −2.01 ms |
| D3 | 55.39 ms | 59.23 ms | +3.84 ms |
| D4 | 43.36 ms | 45.62 ms | +2.26 ms |

Legacy itself got 3–22 ms slower than in the baseline. A higher minimum clock cannot slow the client down,
so this is the internet path to the server getting slower during the morning, not the CPU setting.
Because the configs ran one after another, this scenario can't rank them; only the within-config difference
(hybrid − legacy) is meaningful, and it is noisy. A confirmation run that alternates the configs block by block
was started to fix this (`dynamic_cpu_freq_REPORT.md`).

## Temperature (°C)

| Config | Idle after 120 s | Mean under load | Max under load |
|---|---|---|---|
| BASE | ~45.4 * | ~45.8 * | 45.9 * |
| D1 | 46.2 | 46.6 | 46.9 |
| D2 | 46.6 | 46.7 | 46.9 |
| D3 | 46.2 | 46.0 | 47.8 |
| D4 | 46.6 | 46.5 | 48.1 |

\* baseline readings from the start/end of the baseline run, not 5 s sampling. For reference, the earlier full-speed
setting (`performance`, fixed 1.5 GHz) idled at about 50–52 °C on 2026-11-10 (different day and ambient temperature).

## Findings

1. **Raising only the minimum clock to 750 MHz (D1) removes the hybrid penalty on the LAN**: hybrid 16.59 ms vs
   27.95 ms at the default (−41 %), and hybrid is no slower than legacy (−1.1 ms). Raising the minimum further
   (D2: 1250 MHz) or using schedutil with 750 MHz (D4) gives no additional benefit.
2. **schedutil on its own (D3) is worse than the default**: it reacts too slowly for sub-50 ms bursts
   (hybrid +5.9 ms vs BASE).
3. **Heat cost is small**: all settings stay within ~46–48 °C, about +1 to +2 °C over the default, compared with
   ~50–52 °C at full speed.
4. **Best candidate: D1 (ondemand, min 750 MHz)**: the lowest minimum that gives the full gain, and the coolest
   of the effective settings.
