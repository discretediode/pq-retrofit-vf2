# Possible solutions: closing the remaining latency gap

> **Status (2026-11-12): the latency gap is closed.** With the minimum CPU clock at 750 MHz, the Zba/Zbb build and
> ML-DSA verified on a second core, full hybrid TLS takes 14.5 ms vs 21.0 ms for the legacy baseline (−31 %).
> See [`../results/FINAL_COMPARISON_REPORT.md`](../results/FINAL_COMPARISON_REPORT.md). The section below is the
> planning record from before that result.

This document lists the optimisations we have **tried**, are **trying**, and **could try next**.
They aim to make hybrid post-quantum TLS on the StarFive VisionFive 2 no slower than the legacy (RSA) handshake.

- Measured results: [`../achieved_results/`](../achieved_results/). Start with [`SUMMARY.md`](../achieved_results/SUMMARY.md).
- Everything marked **estimate** below is not yet measured on the board.

---

## 1. Where we stand

**Setup:**
- **Legacy:** X25519 key exchange + RSA-3072 server authentication.
- **Hybrid:** X25519MLKEM768 key exchange + dual-signature authentication.
  - The dual signature is ECDSA P-256 + ML-DSA-44.
  - The ML-DSA-44 signature covers the TLS exporter, so it is bound to the session.
  - It is sent in binary, with no extra round trip.
- **Stack:** OpenSSL 3.5.9 on the board.

| Comparison (end-to-end median, own server on the LAN) | Legacy | Hybrid | Hybrid − legacy |
|---|---|---|---|
| Both at default clock (`ondemand`, min 312.5 MHz) | 21.30 ms | 27.84 ms | **+6.5 ms** ❌ |
| Both at `ondemand` + min 750 MHz (recommended setting) | 15.70 ms | 16.19 ms | **+0.5 ms** 🟡 |
| Both at `performance` (fixed 1.5 GHz) | 10.66 ms | 10.32 ms | −0.3 ms ✅ (within noise) |
| Hybrid at 750 MHz min vs legacy at default | 21.30 ms | 16.19 ms | −5.1 ms ✅ |

**The goal:** hybrid ≤ legacy **at the same CPU setting**. The remaining gap is about **+0.5 ms** on the LAN and about **+3 ms** against the hackathon server over the internet.

### Where the hybrid's extra time goes (client = the board, 1.5 GHz)

| Client-side work | Legacy | Hybrid |
|---|---|---|
| Key exchange | X25519 | X25519 + ML-KEM-768 keygen (1.43 ms) + decaps (1.61 ms) |
| Server authentication | RSA-3072 verify (0.65 ms) | ECDSA P-256 verify (1.41 ms) + ML-DSA-44 verify (0.99 ms) |
| Bytes received / sent | 2,520 / 330 | 5,105 / 1,552 |

Hybrid adds about **4.8 ms of client-side crypto** at full clock. Anything that removes or hides about 1 ms of it closes the LAN gap at equal settings.

---

## 2. Ranked solutions

Ranked by **expected gain ÷ effort**. Gains are per full handshake on the board.

| Rank | Solution | Effort | Expected gain | Status |
|---|---|---|---|---|
| 1 | Raise the minimum CPU clock to 750 MHz | Very low | −11.6 ms hybrid (measured) | ✅ **Done, measured** (not yet permanent) |
| 2 | Skip the redundant certificate-signature check for the pinned certificate | Low | ~1.4 ms hybrid, ~0.65 ms legacy (**estimate**) | 🔲 To try |
| 3 | Pre-generate the ML-KEM key pair before connecting | Medium | ~1.4 ms (**estimate**) | 🔲 To try |
| 4 | Rebuild OpenSSL with Zbb bit-manipulation (`-march=rv64gc_zba_zbb`) | Low (~35 min build) | 0.2–0.6 ms (**estimate**) | 🔄 Running |
| 5 | 0-RTT early data on resumed sessions (own server) | Medium | ~1 network round trip on repeat connections (**estimate**) | 🔲 To try |
| 6 | Verify ECDSA and ML-DSA in parallel on two cores | Medium–high | ≤ 1 ms (**estimate**) | 🔲 Optional |
| 7 | Cache the pinned ML-DSA public key in expanded form | High | 0.3–0.5 ms (**estimate**) | 🔲 Optional |
| 8 | Chip crypto engine through AF_ALG (AES / SHA-2 hardware) | Medium | ~0 for a handshake (**expected negative**) | 🔲 Measure for the report |
| 9 | GPU (OpenCL) / NPU offload | High | Negative (**estimate**) | ❌ Not pursued |
| — | Already done | — | — | ✅ Fast P-256 build (ECDSA verify 2.9× faster); ML-DSA-44 instead of 65; binary signature; session resumption |

---

## 3. Details

### 1. Raise the minimum CPU clock ✅ measured

- **What:** keep the power-saving `ondemand` governor, but set `scaling_min_freq` = 750 MHz instead of 312.5 MHz.
- **Why:** a handshake is a few ms of burst work. It used to start at the lowest clock, which slowed the hybrid (more client crypto) more than the legacy client.
- **Result (1000 runs per mode, alternating blocks):**
  - Hybrid: 27.84 → 16.19 ms.
  - Legacy: 21.30 → 15.70 ms.
  - Idle temperature: +0.5 °C.
- **Remaining work:**
  - Make it permanent with a systemd unit.
  - Test 500 and 625 MHz, which might give the same gain with less heat.

```bash
echo 750000 | sudo tee /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq
```

### 2. Skip the redundant certificate-signature check 🔲

- **What:** the client already **pins** the server certificate. If it also runs normal X.509 chain verification, it does one extra signature check per handshake: the self-signed certificate's own signature.
  - That's one ECDSA verify (~1.4 ms) for the hybrid.
  - And one RSA verify (~0.65 ms) for legacy.
- **How:**
  1. Check whether `hybrid_client` currently does chain verification (`SSL_VERIFY_PEER` + a trust store).
  2. If it does, compare the received certificate with the pinned one byte for byte (or by SHA-256 hash) and skip the chain check.
  3. The **CertificateVerify** signature (proof the server holds the private key) is still checked by TLS, so security is unchanged.
- **Fairness:** apply the same change to the legacy client, then re-measure both. The hybrid gains more because its extra verify is the slower one.
- **Risk:** if the client is not doing chain verification today, this gives 0. Check first.

### 3. Pre-generate the ML-KEM key pair 🔲

- **What:** ML-KEM-768 key generation (1.43 ms) is the largest single item on the client's critical path. Do it on an idle core *before* the connection starts, and keep a small pool of fresh key pairs ready.
- **Security:** each key pair is still used for exactly one connection and kept secret. That's the same security as generating it on demand.
- **How (OpenSSL):** OpenSSL has no API to hand in a pre-made key share. Options:
  1. **A small OpenSSL provider** that wraps ML-KEM-768 keygen and returns a key from a pool filled by a background thread. This is the cleanest option.
  2. **Wrap the X25519MLKEM768 group** the same way, so both halves come pre-made (X25519 keygen is cheap, so ML-KEM is what matters).
- **Expected:** −1.4 ms hybrid. That alone would turn the +0.5 ms LAN gap into about −0.9 ms at equal settings.
- **It also helps resumed sessions:** resumed hybrid is +1.4 ms slower than resumed legacy, because the fresh ML-KEM exchange still runs. A key pool removes most of that.

### 4. Zbb bit-manipulation rebuild 🔄

- **What:** the U74 cores support `zba`/`zbb`, which includes rotate in a single instruction. Keccak/SHAKE, used heavily inside ML-KEM and ML-DSA, is mostly 64-bit rotations: 3 instructions each without Zbb, 1 with it.
- **How:** rebuild OpenSSL 3.5.9 with `-march=rv64gc_zba_zbb -mtune=sifive-7-series`, then rerun the alternating 1000-run benchmark at the same CPU setting.
- **Check first:** OpenSSL may already detect some RISC-V extensions when it runs (`OPENSSL_riscvcap`). If its Keccak already uses Zbb, the gain is ~0. That's still a valid measured result.
- **Pitch value:** "hardware-aware acceleration using the chip's own instruction extensions."

### 5. 0-RTT early data on resumed sessions 🔲

- **What:** with TLS 1.3 resumption, the client can send its first request in the very first flight. That saves one full round trip, which is the dominant cost over the internet (~40 ms).
- **Feasible because:** we control the hybrid server (`SSL_CTX_set_max_early_data` on the server; `SSL_write_early_data` on the client).
- **Caveat:** early data can be replayed by an attacker. Use it only for idempotent requests.
- **Pitch value:** "post-quantum on first contact, faster than legacy on every reconnect."

### 6. Verify ECDSA and ML-DSA in parallel 🔲

- **What:** run the two independent signature checks on two cores, so the cost is max(1.41, 0.99) instead of the sum.
- **Problem:** in our protocol the ECDSA check happens inside the TLS handshake, while the ML-DSA signature only arrives with the first HTTP response, because it signs the exporter, which exists only after the handshake. To run them in parallel, ECDSA verification would have to be deferred out of OpenSSL's handshake code.
- **Verdict:** possible, but invasive. Solutions 2 and 3 give similar gains for less work.

### 7. Cache the pinned ML-DSA key in expanded form 🔲

- **What:** every ML-DSA verify re-expands a large matrix from the public key using SHAKE. The key is pinned, so this can be done once.
- **Effort:** high. OpenSSL doesn't expose this, so it needs our own ML-DSA code (e.g. `mldsa-native`). Our Mbed TLS harness in `src/` is a starting point.
- **Expected:** 30–50% of ML-DSA verify time, about 0.3–0.5 ms.

### 8. Chip crypto engine through AF_ALG 🔲 (expected negative result)

- **What:** the JH7110 has AES / SHA-2 / RSA hardware, reachable through the Linux kernel's AF_ALG interface.
- **Why it probably won't help a handshake:**
  - The system-call overhead per operation is larger than the tiny amount of data hashed.
  - It has no SHAKE (needed by ML-KEM/ML-DSA) and no elliptic curves.
- **Why measure it anyway:** it gives the challenge's "data-transfer and setup cost" question a real number from hardware we actually have. Check `/proc/crypto` for `starfive` entries first.

### 9. GPU / NPU offload ❌ not pursued

- All client crypto is about 5 ms in total, so even infinitely fast offload saves at most that.
- Per-call transfer and setup overhead (PCIe for an NPU, OpenCL for the GPU) is likely in the same range.
- The arithmetic doesn't fit neural-network accelerators: ML-KEM works mod 3329, ML-DSA mod 8380417, and both rely on Keccak.
- The board's OpenCL driver support is unverified. Full reasoning: `achieved_results/SUMMARY.md`, section 6.

---

## 4. Open questions / risks

| Question | Why it matters | Action |
|---|---|---|
| Does the challenge require **Mbed TLS**, or is OpenSSL accepted? | The task sheet names Mbed TLS. Our results use OpenSSL 3.5. | Ask the organisers. The Mbed TLS benchmark harness in `src/` + `Makefile` is our fallback evidence. |
| Is a **custom dual-signature** construction acceptable? | It is not a standard TLS signature algorithm. | Present the standards route: composite ML-DSA certificates (IETF LAMPS draft) or X.509 alternative-key certificates. |
| RSA-3072 vs RSA-2048 baseline | With RSA-2048, legacy signing is ~3× faster. | Keep RSA-3072 (it's what the hackathon server uses), but have the RSA-2048 number ready. |
| Source code is not in this repo yet | Judges can't reproduce the results without it. | Push `hybrid_server`, `hybrid_client`, `bench_final.sh` and the OpenSSL build steps. |

---

## 5. How each solution is measured

Use the same method as `achieved_results/`, so the numbers are comparable:

1. **Same CPU setting for legacy and hybrid** (recommended: `ondemand` + min 750 MHz). A gain only counts if it holds at equal settings.
2. **Alternating blocks** of 50 runs (legacy → hybrid, then hybrid → legacy), 1000 runs per mode, so both see the same network.
3. Report **best / median / p95 / worst**, plus "hybrid faster in X of 20 blocks".
4. **Correctness check per block:** X25519MLKEM768 negotiated, ML-DSA-44 signature verified, 0 failed connections.
5. Results go in a new folder under `achieved_results/<solution_name>/` with a `REPORT.md`, raw CSV, stats and log.
