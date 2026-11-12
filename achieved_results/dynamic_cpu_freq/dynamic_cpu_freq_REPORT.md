# DYNAMIC CPU FREQ: final report (raise only the minimum CPU clock)

Date: 2026-11-11. Board: VisionFive 2 (JH7110, 4x U74 @ up to 1.5 GHz).
Compared with the **BASELINE** (`baseline_REPORT.md`): default CPU setting (ondemand, min 312.5 MHz),
legacy vs hybrid, 1000 runs per mode.

## The idea
Under the default `ondemand` setting the CPU sits at its lowest clock and a TLS handshake (a burst of a few ms)
is over before the governor speeds up. Full speed (`performance`, fixed 1.5 GHz) fixes that but heats the chip.
**Middle ground: keep `ondemand`, but raise only the minimum clock** (`scaling_min_freq`), so every handshake
starts at a reasonable speed while the CPU can still save power.

## Part 1: screening (`dynamic_cpu_freq_screen_REPORT.md`)
Four settings, 500 runs per mode each. On the LAN, ondemand + min 750 MHz (D1), min 1250 MHz (D2) and
schedutil + min 750 MHz (D4) all cut the hybrid time by ~41 %; schedutil alone (D3) was worse than the default.
**D1 was selected**: the lowest minimum with the full gain and the least heat.

## Part 2: confirmation, D1 vs default, alternated block by block
Method: 20 blocks; in every block both CPU settings were measured back to back (order alternating), each with
50 legacy + 50 hybrid connections per scenario, so both settings saw the same network conditions.
1000 measurements per setting, mode and scenario; 8000 connections, 0 failures; every hybrid block verified
(X25519MLKEM768 negotiated, ML-DSA-44 verified). Idle temperatures measured back to back in the same session.
The CPU setting was restored to the default at the end.
Files: `dynamic_cpu_freq_confirm_raw.csv`, `dynamic_cpu_freq_confirm_temps.csv`, `dynamic_cpu_freq_confirm.log`,
`dynamic_cpu_freq_confirm_stats.txt`.

| Setting | | Governor | Minimum clock |
|---|---|---|---|
| D0 | default (= baseline setting) | ondemand | 312.5 MHz |
| **D1** | **dynamic CPU freq** | ondemand | **750 MHz** |

### Scenario 1: hackathon server (IPv4, 193.30.40.242), TLS handshake time

| Setting | Mode | Best | **Median** | 95th pct | Worst |
|---|---|---|---|---|---|
| D0 default | Legacy (X25519) | 27.01 ms | **42.17 ms** | 82.42 ms | 407.89 ms |
| D0 default | Hybrid (X25519MLKEM768) | 32.30 ms | **48.30 ms** | 91.38 ms | 873.22 ms |
| **D1 min 750 MHz** | Legacy (X25519) | 26.10 ms | **37.16 ms** | 68.83 ms | 303.55 ms |
| **D1 min 750 MHz** | Hybrid (X25519MLKEM768) | 29.25 ms | **40.26 ms** | 75.03 ms | 200.35 ms |

### Scenario 2: own server (LAN), end-to-end time (hybrid = ML-KEM + ECDSA P-256 + ML-DSA-44)

| Setting | Mode | Best | **Median** | 95th pct | Worst |
|---|---|---|---|---|---|
| D0 default | Legacy (X25519 + RSA-3072) | 12.54 ms | **21.30 ms** | 24.26 ms | 30.59 ms |
| D0 default | Hybrid | 20.12 ms | **27.84 ms** | 31.55 ms | 40.21 ms |
| **D1 min 750 MHz** | Legacy (X25519 + RSA-3072) | 8.91 ms | **15.70 ms** | 18.04 ms | 21.74 ms |
| **D1 min 750 MHz** | Hybrid | 13.93 ms | **16.19 ms** | 19.22 ms | 24.33 ms |

### Paired per-block comparison (same block = same network conditions)

| Comparison | Hackathon server | Own server (LAN) |
|---|---|---|
| Hybrid: D1 vs D0 | **−8.6 ms** median, D1 faster in 18/20 blocks | **−11.6 ms**, D1 faster in 20/20 blocks |
| Legacy: D1 vs D0 | −6.5 ms, D1 faster in 17/20 blocks | −5.6 ms, D1 faster in 20/20 blocks |
| Hybrid − legacy at D0 | +5.9 ms (hybrid faster in 0/20) | +6.4 ms (0/20) |
| Hybrid − legacy at D1 | +4.5 ms (hybrid faster in 4/20) | **+0.5 ms** (4/20) |

### Temperature (°C)

| Setting | Idle after 120 s | Under load, mean / max |
|---|---|---|
| D0 default | 44.7 | 44.7 / 46.4 |
| D1 min 750 MHz | 45.2 | 44.6 / 46.3 |

For reference: full speed (`performance`, 1.5 GHz) idled at ~50–52 °C (2026-11-10).

## Results against the target (baseline = legacy at the default clock)

| Scenario | Baseline: legacy at default (D0) | Hybrid with dynamic CPU freq (D1) | Change |
|---|---|---|---|
| Hackathon server, handshake | 42.17 ms | **40.26 ms** | **−1.9 ms (−4.5 %)** ✅ |
| Own server, end-to-end (full hybrid auth) | 21.30 ms | **16.19 ms** | **−5.1 ms (−24 %)** ✅ |

## Findings

1. **The target is met with almost no heat cost.** With only the minimum clock raised to 750 MHz, hybrid PQC is
   faster than the legacy baseline at the default setting: −1.9 ms against the hackathon server and −5.1 ms for
   the full hybrid authentication on the LAN. The idle temperature rises by only +0.5 °C (45.2 vs 44.7 °C),
   versus about +6 °C for full speed.
2. **The hybrid penalty almost disappears on the device side.** At the default setting hybrid costs +6.4 ms on the
   LAN; at D1 only +0.5 ms. The remaining gap against the internet server (+3–4.5 ms) is mostly the larger ML-KEM
   messages on the network path.
3. **Reproducible.** The default setting re-measured in this run (D0) matches the baseline from earlier in the day
   (LAN: 21.30 vs 21.61 ms legacy, 27.84 vs 27.95 ms hybrid).
4. **Honest framing.** The CPU setting also speeds up legacy (−5.6 ms on the LAN). Under the same CPU setting hybrid is
   still slightly slower than legacy (+0.5 ms LAN, +3 to +4.5 ms internet). The claim is therefore: with a modest
   clock-policy change, hybrid PQC on this board is faster than the current (default) legacy deployment, at
   ~0.5 °C extra.
5. **Worst cases come from the network.** Single spikes of 200–870 ms against the internet server appear in both
   modes and both settings; on the LAN the worst case is 24 ms (D1 hybrid).

## RECOMMENDATION: which CPU clock setting to use

**Optimal setting (of those tested): `ondemand` governor with the minimum clock raised to 750 MHz (D1).**
It's the lowest minimum that gives the full speed-up, it's the coolest of the settings that work, and it keeps
the power-saving governor (the CPU still goes up to 1.5 GHz when needed and stays at 750 MHz when idle).

### All settings compared: own server (LAN), full hybrid authentication, end-to-end median

| Setting | Governor / clock | Legacy | Hybrid | Hybrid − legacy | Idle temp | Runs per mode | Verdict |
|---|---|---|---|---|---|---|---|
| Default (baseline) | ondemand, min 312.5 MHz | 21.30 ms | 27.84 ms | +6.5 ms | 44.7 °C | 1000 | ❌ hybrid slower than baseline |
| **D1** | **ondemand, min 750 MHz** | **15.70 ms** | **16.19 ms** | **+0.5 ms** | **45.2 °C** | **1000** | ✅ **recommended** |
| D2 | ondemand, min 1250 MHz | 17.54 ms | 16.67 ms | −0.9 ms | 46.6 °C | 500 | ✅ same speed, warmer |
| D3 | schedutil, min 312.5 MHz | 23.90 ms | 33.85 ms | +10.0 ms | 46.2 °C | 500 | ❌ worse than default |
| D4 | schedutil, min 750 MHz | 17.58 ms | 16.54 ms | −1.0 ms | 46.6 °C | 500 | ✅ same speed, warmer |
| Full speed | performance, fixed 1.5 GHz | 10.66 ms | 10.32 ms | −0.3 ms | ~50–52 °C | 100 | ✅ fastest, hottest |

Notes on comparability:
- Default and D1 come from the same alternating 1000-run confirmation, so they're directly comparable.
- D2–D4 come from the screening run (sequential, 500 runs); their LAN numbers are reliable, and their
  temperatures were taken the same morning.
- Full speed was measured on 2026-11-10 (50 runs x 2 passes, different day and room temperature), so treat its
  numbers as indicative.
- Against the hackathon server (internet), only the default vs D1 comparison is clean: hybrid 48.30 → 40.26 ms,
  below the legacy baseline of 42.17 ms.

### Why D1 and not the others
| Option | Why not chosen |
|---|---|
| Default (min 312.5 MHz) | Handshakes start at the lowest clock, so hybrid is +6.5 ms slower than legacy and misses the target. |
| D2 (min 1250 MHz), D4 (schedutil + 750 MHz) | Same latency as D1 (within ~0.5 ms) but about 1 °C warmer at idle. No benefit. |
| D3 (schedutil alone) | Reacts too slowly for handshakes lasting a few ms; worse than the default. |
| Full speed (1.5 GHz fixed) | About 6 ms faster again on the LAN, but ~5–7 °C hotter at idle; the user switched it off because of heat. Use it only if a heatsink/fan is fitted and latency matters most. |

### Choosing by priority
| Priority | Use |
|---|---|
| Lowest latency, heat not a problem (heatsink/fan) | `performance` (fixed 1.5 GHz) |
| **Meet the latency target with minimal heat (recommended)** | **`ondemand` + min 750 MHz** |
| Lowest heat/energy, latency not critical | default `ondemand` (min 312.5 MHz) |

### Not yet tested
- Minimum clocks of 500 MHz and 625 MHz: a lower minimum might give the same gain with even less heat.
  750 MHz is optimal among the tested settings, not proven to be the absolute optimum.
- Energy use (needs a USB power meter). Temperature was used as the proxy.

## How to apply D1 on the board
```
echo 750000 | sudo tee /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq     # now (until reboot)
echo 312500 | sudo tee /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq     # back to default
```
Current state: **default** (min 312.5 MHz). D1 isn't made permanent.
