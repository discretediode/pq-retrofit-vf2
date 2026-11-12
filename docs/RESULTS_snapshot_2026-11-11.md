> **Archived snapshot (2026-11-11), superseded.** Kept so no work is lost. Current results:
> [`../results/FINAL_COMPARISON_REPORT.md`](../results/FINAL_COMPARISON_REPORT.md) and the [main README](../README.md).

# Results summary: hybrid post-quantum TLS on the StarFive VisionFive 2

NextSemi / SEMICON FOCAL hackathon, "Post-Quantum Retrofit". Status as of 2026-11-11.

- One-page overview of what we found.
- Full data and reports: [`achieved_results/`](achieved_results/). Start with [`SUMMARY.md`](achieved_results/SUMMARY.md).
- What we're trying next: [`possible_solutions/`](possible_solutions/).

---

## In short

- ✅ **Hybrid post-quantum TLS works on the board:**
  - **X25519 + ML-KEM-768** key exchange;
  - **ECDSA P-256 + ML-DSA-44** authentication.
  - Both signatures are verified on every connection.
  - 0 failed connections in the 20,000 handshakes of our 1000-run benchmarks.
- ✅ **With our recommended CPU setting, hybrid is faster than today's legacy setup.**
  - Hybrid takes **16.19 ms** end-to-end vs **21.30 ms** for legacy RSA at the default setting.
  - The cost is +0.5 °C idle temperature.
- 🟡 **At identical CPU settings, hybrid costs almost nothing extra:**
  - **+0.5 ms (+3%)** on the LAN at the recommended setting;
  - **equal** at full clock (10.32 vs 10.66 ms, within noise).
  - We are still working on closing the last 0.5 ms.
- 💡 **The main bottleneck was not post-quantum crypto.** It was:
  1. the CPU clock policy;
  2. a slow default ECDSA implementation on RISC-V.
  Fixing both mattered more than any choice of algorithm.

---

## 1. What we built

| | Legacy (baseline) | Hybrid (ours) |
|---|---|---|
| Key exchange | X25519 | **X25519MLKEM768** (X25519 + ML-KEM-768) |
| Server authentication | RSA-3072 | **ECDSA P-256 + ML-DSA-44**; the client accepts only if **both** verify |
| TLS library | OpenSSL 3.5.9 (built on the board) | OpenSSL 3.5.9 with fast P-256 (`enable-ec_nistp_64_gcc_128`) |

How the hybrid authentication works:
1. A normal TLS 1.3 handshake, in which the server signs with **ECDSA P-256**.
2. The server also signs `"hybrid-auth-v1" || TLS-exporter` with **ML-DSA-44**. The exporter value is unique to this session, so the signature can't be replayed elsewhere.
3. The ML-DSA signature (2,420 bytes, raw binary) travels in the first HTTP response, so there's **no extra round trip**.
4. Legacy RSA clients still work against the same server.

Security checks passed: a wrong ML-DSA key is rejected; a wrong pinned certificate is rejected.

**Hardware:** VisionFive 2 with the JH7110 chip: 4× SiFive U74 at up to 1.5 GHz, RV64GC + Zba/Zbb, no vector unit. Debian. Server on a Mac over local Ethernet (~1 ms round trip).

---

## 2. Headline results

End-to-end time on the LAN, including full hybrid authentication. Medians of 1000 runs per mode; legacy and hybrid were alternated in blocks of 50 so both saw the same network.

| CPU setting | Legacy | Hybrid | Hybrid − legacy | Idle temp |
|---|---|---|---|---|
| Default (`ondemand`, min 312.5 MHz) | 21.30 ms | 27.84 ms | +6.5 ms ❌ | 44.7 °C |
| **Recommended (`ondemand`, min 750 MHz)** | 15.70 ms | **16.19 ms** | **+0.5 ms** | **45.2 °C** |
| Full clock (`performance`, 1.5 GHz)¹ | 10.66 ms | **10.32 ms** | −0.3 ms ✅ | ~50–52 °C |

¹ 50 runs × 2 passes, on a different day. Treat as indicative.

**The fair claim:**
- Moving from today's default to our recommended setting makes hybrid PQC **5.1 ms faster than the current legacy setup**, and it keeps the power-saving governor.
- At equal settings, the post-quantum upgrade costs **0.5 ms or less**.

---

## 3. How we got there (optimisation steps)

All at full clock, own server on the LAN, end-to-end median:

| Step | Configuration | End-to-end | vs legacy |
|---|---|---|---|
| A | **Legacy:** X25519 + RSA-3072 | 10.66 ms | — |
| B | First hybrid: X25519MLKEM768 + ECDSA + **ML-DSA-65** | 14.24 ms | +3.58 ms |
| C | + fast P-256 build flag | 11.86 ms | +1.20 ms |
| D | + **ML-DSA-44** instead of ML-DSA-65 | 10.24 ms | −0.42 ms |
| E | + binary signature instead of base64 | 10.32 ms | −0.34 ms |
| — | + CPU min clock 750 MHz (measured at the default governor, section 2) | — | turns +6.5 ms into +0.5 ms |

**Takeaways:**
- **Fast P-256 build: −2.4 ms.** OpenSSL's default build uses slow generic elliptic-curve code on RISC-V. One build flag made ECDSA verify **2.9× faster** (4.11 → 1.41 ms).
- **ML-DSA-44 instead of ML-DSA-65: −1.6 ms**, and 900 bytes smaller.
  - The verify is 0.6 ms faster.
  - The handshake also moves less data.
  - Its security level matches P-256, the same pairing the IETF composite-signature draft uses.
- **Binary signature:** saves ~800 bytes on the wire. The time effect is within noise.

---

## 4. Signature cost: hybrid is cheaper to sign, dearer to verify

Measured on the board at 1.5 GHz:

| Operation | RSA-3072 (legacy) | ECDSA P-256 + ML-DSA-44 (hybrid) |
|---|---|---|
| Sign | 34.1 ms | **5.3 ms** (0.64 + 4.69), **6× faster** |
| Verify | **0.65 ms** | 2.4 ms (1.41 + 0.99) |
| Sign + verify | 34.8 ms | **7.7 ms**, **4.5× faster overall** |
| Signature size | 384 B | 2,491 B (71 + 2,420) |

- The board is the *client*, so it only **verifies**. For the board, hybrid authentication is more work. That's where the remaining +0.5 ms comes from.
- For devices that **sign** (device certificates, mutual TLS), hybrid wins clearly: 5.3 ms vs 34.1 ms.

---

## 5. Against the real hackathon server (internet)

The hackathon server supports hybrid **key exchange** only; its authentication is RSA-3072 in both modes. Medians of 1000 runs per mode, alternating:

| CPU setting | Legacy (X25519) | Hybrid (X25519MLKEM768) | Difference |
|---|---|---|---|
| Default | 42.17 ms | 48.30 ms | +6.1 ms |
| **Min 750 MHz** | 37.16 ms | **40.26 ms** | +3.1 ms |

Server-side confirmation: `"handshake_method":"hybrid-pqc"`.

- About 40 ms of each handshake is network round trip, so the crypto is a small share.
- Single spikes (up to 524 ms) come from the network, not the crypto: the 99th percentiles are almost identical (105 vs 104 ms).

---

## 6. Other measurements

| | Legacy | Hybrid | Note |
|---|---|---|---|
| Resumed session (repeat connection) | 4.60 ms | 5.98 ms | No signatures; still a fresh post-quantum key exchange. Both are ~2× faster than a full handshake. |
| Bytes received / sent per connection | 2,520 / 330 | 5,105 / 1,552 | +2.6 KB / +1.2 KB: ML-DSA signature, ML-KEM key and ciphertext |
| Peak memory (max RSS) | 6.3–6.6 MB in every configuration | | Hybrid adds about +0.1–0.2 MB |
| Idle temperature (recommended setting) | — | 45.2 °C | vs 44.7 °C default, ~50–52 °C full clock |

---

## 7. Findings

1. **Hybrid post-quantum TLS is practical on a low-cost RISC-V board today**, with off-the-shelf OpenSSL 3.5.
2. **CPU clock policy matters as much as the algorithms.**
   - With the default `ondemand` governor, each short handshake starts at 312–375 MHz.
   - That stretches the hybrid's larger client-side crypto the most.
   - Raising only the *minimum* clock to 750 MHz fixes it for +0.5 °C.
   - `schedutil` without a raised minimum was worse than the default.
3. **The classical part was the bottleneck, not the post-quantum part.** ECDSA verify (4.11 ms) was slower than ML-DSA-44 verify (0.99 ms) until we fixed the build.
4. **Pick matching security levels.** ML-DSA-44 + P-256 is both sufficient and faster and smaller than ML-DSA-65.
5. **Hardware offload would not pay off here.**
   - All client crypto totals about 5 ms.
   - Accelerator transfer and setup costs would be similar.
   - The arithmetic (mod 3329 / mod 8380417, Keccak) doesn't suit neural-network accelerators. This is an assessment, not a measurement.
6. **Heat vs latency is a real trade-off for embedded devices.** Full clock is fastest, but runs ~6 °C hotter at idle.

---

## 8. Limitations (stated openly)

- **TLS library:** our measurements use OpenSSL 3.5.9. The task sheet names Mbed TLS. The Mbed TLS benchmark harness is in `src/` + `Makefile`.
- **Custom construction:** the dual signature is our own scheme, not a standard TLS signature algorithm. Certificates and the ML-DSA key are pinned, not issued by a PKI.
  - Standards route: composite ML-DSA certificates (IETF LAMPS draft) or X.509 alternative-key certificates.
- **Own server:** the hackathon server only supports RSA authentication, so full hybrid authentication was measured against our own server on the LAN.
- **Equal-settings gap:** hybrid is still +0.5 ms (LAN) / +3.1 ms (internet) slower than legacy at the *same* CPU setting.
- **Not measured:** energy use (temperature used as a proxy); minimum clocks of 500 and 625 MHz; the hardware-offload assessment.

---

## 9. Next steps

See [`possible_solutions/README.md`](possible_solutions/README.md). The top candidates for closing the last 0.5 ms:
1. **Skip the redundant certificate-signature check** for the pinned certificate (~1.4 ms, estimate).
2. **Pre-generate the ML-KEM key pair** before connecting (~1.4 ms, estimate).
3. **Zbb bit-manipulation rebuild** of OpenSSL (0.2–0.6 ms, estimate; running now).

## Where to find everything

| What | Where |
|---|---|
| Full project summary, reproduction steps | [`achieved_results/SUMMARY.md`](achieved_results/SUMMARY.md) |
| Default-clock baseline, 1000 runs | [`achieved_results/baseline_default_clock/`](achieved_results/baseline_default_clock/) |
| CPU clock study + recommendation | [`achieved_results/dynamic_cpu_freq/`](achieved_results/dynamic_cpu_freq/) |
| Planned optimisations | [`possible_solutions/`](possible_solutions/) |
| Mbed TLS benchmark harness | [`src/`](src/), [`Makefile`](Makefile), [`HANDOFF.md`](HANDOFF.md) |
