# Final comparison: legacy vs hybrid PQC for every approach

StarFive VisionFive 2 (4 × SiFive U74 RISC-V, Debian, OpenSSL 3.5.9). All values are milliseconds per connection,
**1000 connections per mode**; lower is better. Legacy = X25519 + RSA-3072 (the baseline connection).
Hybrid = X25519 + ML-KEM-768 key exchange, plus ECDSA P-256 + ML-DSA-44 authentication on our own server.
Detailed reports, raw data and scripts: `results/`.

## 1. Our own server (full hybrid: key exchange + authentication, local network)

| Setting | Legacy best | Legacy **median** | Legacy worst | Hybrid best | Hybrid **median** | Hybrid worst |
|---|---|---|---|---|---|---|
| **Default CPU (baseline)** | 14.8 | **21.6** | 31.3 | 20.5 | **28.0** | 40.1 |
| CPU bump, 750 MHz minimum | 8.9 | **15.7** | 21.7 | 13.9 | **16.2** | 24.3 |
| Bit manipulation (Zba/Zbb) | 11.7 | **21.1** | 33.3 | 19.2 | **26.6** | 38.4 |
| Multicore (2nd core for ML-DSA) | 15.4 | **21.7** | 35.3 | 17.5 | **24.6** | 38.1 |
| **All three combined** | 9.0 | **15.4** | 21.4 | **12.2** | **14.5** | **26.6** |

## 2. FOCAL's server (hybrid key exchange, over the internet)

| Setting | Legacy best | Legacy **median** | Legacy worst | Hybrid best | Hybrid **median** | Hybrid worst |
|---|---|---|---|---|---|---|
| **Default CPU (baseline)** | 31.6 | **39.9** | 524.5 | 36.3 | **45.1** | 359.3 |
| CPU bump, 750 MHz minimum | 26.1 | **37.2** | 303.6 | 29.3 | **40.3** | 200.4 |
| Bit manipulation (Zba/Zbb) | 31.0 | **41.3** | 274.6 | 34.0 | **47.5** | 547.6 |
| Multicore | not possible: FOCAL's server supports the hybrid key exchange, but only RSA authentication | | | | | |
| **Combined** (clock + bitmanip; multicore n/a) | 25.9 | **33.0** | 355.0 | **27.7** | **35.6** | **140.8** |

On FOCAL's server, bit manipulation shows no clear change: the internet delay (about 30 ms) hides a 1–2 ms gain.

## 3. Final comparison (baseline and combined measured in the same session: the fairest)

| | Best | **Median** | Worst |
|---|---|---|---|
| Own server, legacy baseline | 12.6 | **21.0** | 35.1 |
| Own server, **hybrid combined** | **12.2** | **14.5** | **26.6** |
| FOCAL, legacy baseline | 28.2 | **38.4** | 354.3 |
| FOCAL, **hybrid combined** (clock + bitmanip) | **27.7** | **35.6** | **140.8** |

"Combined" = all three approaches on our own server. On FOCAL's server only the CPU clock and bit manipulation
apply, because it uses RSA authentication, so there's no ML-DSA signature to verify on a second core.

**Hybrid combined beats the legacy baseline on best, median and worst:** −31 % median on our own server
(faster in 20/20 test blocks) and −7 % median on FOCAL's server (17/20 blocks). 8,000 connections, 0 failures,
+1.3 °C idle temperature.

## 4. How to read best / median / worst
- **Best:** the fastest single connection, what the board can do with no disturbance.
- **Median:** the typical connection. This is the headline number and the fairest one.
- **Worst:** a single connection out of 1000, so it's random.
  - On **our own server** it's meaningful (local network, little noise): hybrid worst 40.1 → 26.6 ms, so the slow
    cases improved too.
  - On **FOCAL's server**, worst values (140–548 ms) come from one-off internet delays, not from the crypto. Don't
    compare them as results; that's why we report the median.

## 5. What each approach does to the hybrid median (own server)

| Approach | Hybrid median | Change | Why |
|---|---|---|---|
| Default CPU (baseline) | 28.0 | | CPU idles at 312.5 MHz; a 20 ms handshake ends before it speeds up |
| CPU bump, 750 MHz minimum | 16.2 | **−11.6 ms** | Biggest win: the crypto no longer starts at the lowest clock |
| Bit manipulation | 26.6 | −1.6 ms | Single-instruction rotations: SHA-3 +50 %, ML-KEM up to −19 % |
| Multicore | 24.6 | −3.6 ms | ECDSA and ML-DSA checked at the same time on two cores |
| **All three combined** | **14.5** | **−13.6 ms** | **Below legacy's 21: 31 % faster than the baseline** |

## 6. Quick memory
- **Own server, hybrid median:** 28 → 16.2 (bump) · 26.6 (bitmanip) · 24.6 (multicore) · **14.5 (all)**, vs legacy **21**.
- **Own server, hybrid worst:** 40 → **26.6**.
- **FOCAL, hybrid median:** 45 → **35.6**, vs legacy **38–40**.

## Notes
- Each single-approach row (1 and 2) comes from its own test session, so its legacy values differ slightly
  (21.1–21.7 ms). Section 3 is the cleanest comparison, because baseline and combined were alternated block by block
  in one session.
- Multicore only changes hybrid: legacy has a single signature to check, so its row is just that session's reference.
- Sources (all in `results/`): `01_baseline_default_clock/baseline_stats_1000.txt`,
  `02_dynamic_cpu_freq/dynamic_cpu_freq_confirm_stats.txt` (D1), `03_bitmanip_zba_zbb/bitmanip_e2e_stats.txt`,
  `04_multicore/multicore_e2e_stats.txt` (v2_par), `05_combined_all_approaches/combined_stats.txt`.
