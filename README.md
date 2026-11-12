# Post-Quantum Retrofit on the StarFive VisionFive 2

FOCAL SEMICON hackathon "Post-Quantum Retrofit", Embedded Linux track.
We retrofitted TLS 1.3 on a low-cost RISC-V board (StarFive VisionFive 2: JH7110, 4 × SiFive U74, no vector unit)
to **hybrid post-quantum cryptography**, then made it **faster than the original legacy connection**, in software only.

| TLS 1.3 job | Legacy (baseline) | Hybrid (ours) |
|---|---|---|
| Key exchange | X25519 | **X25519 + ML-KEM-768** (`X25519MLKEM768`) |
| Server authentication | RSA-3072 | **ECDSA P-256 + ML-DSA-44**; both signatures must verify |

## Result

| Scenario (median of 1000 connections) | Legacy baseline | Hybrid, before | **Hybrid, final** | Change vs legacy |
|---|---|---|---|---|
| Full hybrid TLS (key exchange + authentication), own test server | 21.0 ms | 28.1 ms | **14.5 ms** | **−31 %** (faster in 20/20 blocks) |
| Hybrid key exchange, FOCAL's hackathon server | 38.4 ms | 43.8 ms | **35.6 ms** | **−7 %** (faster in 17/20 blocks) |

8,000 connections in the final run, 0 failures; idle temperature +1.3 °C. Memory +0.2 MB; +3.8 KB per handshake.

**How:** at default settings hybrid was 33 % slower. We traced it to three causes and fixed each in software:
1. **CPU clock floor:** the CPU idled at 312.5 MHz during short handshakes, so we raised only the minimum clock to
   750 MHz (28.0 → 16.2 ms).
2. **RISC-V bit manipulation:** we rebuilt OpenSSL 3.5.9 with Zba/Zbb, making SHA-3 50 % faster and ML-KEM up to 19 %
   faster (28.2 → 26.6 ms).
3. **Second core:** the ML-DSA signature travels in the TLS Certificate message and is verified on a second core while
   ECDSA is verified on the first (28.2 → 24.6 ms).

## Repository layout

```
README.md                         start here
results/                          all measurements
├── FINAL_COMPARISON_REPORT       all numbers on one page: best / median / worst, legacy vs hybrid
├── README                        index of every result file
├── 01_baseline_default_clock/    baseline: default CPU setting
├── 02_dynamic_cpu_freq/          approach 1: minimum CPU clock 750 MHz
├── 03_bitmanip_zba_zbb/          approach 2: RISC-V bit-manipulation build
├── 04_multicore/                 approach 3: ML-DSA verified on a second core
└── 05_combined_all_approaches/   final result: all approaches together
presentation/                     slide notes, technical walkthrough, study guide with practice questions
docs/
├── PROJECT_SUMMARY               full project story, bottleneck analysis, migration guide (section 7)
├── POSSIBLE_SOLUTIONS            ideas we evaluated and next steps
└── archive/                      earlier results snapshot
src/                              our C code (OpenSSL 3.5): TLS client and server, benchmarks
tools/                            analysis scripts and Markdown-to-PDF converter
hailo_prototype/                  teammate's Mbed TLS + Hailo-8L acceleration prototype, with its own guide
```

Each folder in `results/` holds a report (`.md` + `.pdf`), all raw measurements, statistics, logs and the
scripts used. The Hailo prototype wasn't used for the results: no Hailo module was detected on our board.

## Limits
- Hybrid authentication was measured against our own test server: FOCAL's server supports the hybrid key exchange
  but authenticates with RSA-3072 only (ECDSA and ML-DSA signatures are refused).
- The ML-DSA signature uses a custom TLS extension (`0xFE20`); the production path is standard ML-DSA or composite
  certificates.
- The bit-manipulation build needs a CPU with Zba/Zbb. Energy wasn't measured (temperature is the proxy).
