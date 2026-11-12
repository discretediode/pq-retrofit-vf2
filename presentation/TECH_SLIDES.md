# Technical slides: content and narration (3 slides, about 2 minutes)

Hybrid post-quantum TLS on the StarFive VisionFive 2 (RISC-V). About 40 seconds per slide.
"On the slide" is what to show; "Say" is the narration. All numbers are medians of 1000 connections per mode.

---

## Slide 1: The retrofit (≈ 40 s)

**Title:** Making a RISC-V IoT board quantum-safe

**On the slide:**

| TLS 1.3 job | Legacy (baseline) | Our hybrid retrofit |
|---|---|---|
| Key exchange | X25519 | **X25519 + ML-KEM-768** |
| Server authentication | RSA-3072 | **ECDSA P-256 + ML-DSA-44** |

- Hybrid = an attacker must break **both** the classical and the post-quantum part
- OpenSSL 3.5 built on the board · ML-DSA signature inside the TLS handshake, bound to the session
- **Problem:** at default settings, hybrid is **+33 % slower** (21.0 → 28.1 ms)

**Say:**
"Quantum computers will break RSA and elliptic curves, and attackers can record traffic today to decrypt later.
IoT devices stay in the field for 15 years, so they must move first. We retrofitted the VisionFive 2 to hybrid TLS:
ML-KEM plus X25519 for the key exchange, and ML-DSA plus ECDSA in place of RSA for authentication. An attacker now has
to break both. But on this board, hybrid was a third slower than the legacy connection. So the real question was:
can we make it quantum-safe *without* making it slower?"

---

## Slide 2: Three software-only speed-ups (≈ 45 s)

**Title:** Finding the time: clock, compiler, cores

**On the slide** (one row per approach, plus a bar chart of the hybrid connection time):

| Approach | Root cause | Fix | Hybrid alone |
|---|---|---|---|
| ① CPU clock floor | Short handshakes finish before the CPU speeds up from 312 MHz | Raise the minimum clock to **750 MHz** | 28.1 → 16.2 ms |
| ② RISC-V bit-manipulation | SHA-3 inside ML-KEM/ML-DSA needs rotates the default build doesn't use | Rebuild with **Zba/Zbb** | SHA-3 **+50 %**, ML-KEM up to **−19 %** |
| ③ Second core | ECDSA and ML-DSA checks ran one after another | Verify them **in parallel** on 2 cores | 28.2 → 24.6 ms |

Chart (hybrid, ms): baseline 28.1 · ① 16.2 · ② 26.6 · ③ 24.6 · **all three 14.5**, with a dashed line at legacy **21.0**. (Single-approach values come from separate runs; "all three" is the final paired run.)

**Say:**
"We profiled where the time goes and found three causes, each with a software fix. First, the CPU idles at 312 MHz and
a 20-millisecond handshake is over before it speeds up, so we raised only the minimum clock to 750 MHz. Second, the
post-quantum algorithms spend much of their time in SHA-3, which needs bit rotations; the board's RISC-V cores have
those instructions, but the default build doesn't use them. Recompiling made SHA-3 50 % faster. Third, the two
signature checks are independent, so we run them on two cores at the same time. No new hardware, no new chip."

---

## Slide 3: Results (≈ 40 s)

**Title:** Quantum-safe *and* faster than before

**On the slide** (big numbers):

| | Legacy baseline | Hybrid, all three fixes | |
|---|---|---|---|
| **Full hybrid TLS** (own server) | 21.0 ms | **14.5 ms** | **−31 %** |
| **Hybrid key exchange** (hackathon server) | 38.4 ms | **35.6 ms** | **−7 %** |

- 8,000 test connections, **0 failures**, alternating test blocks
- Cost: **+1.3 °C** idle · **+0.2 MB** memory · **+3.8 KB** per handshake
- Delivered: migration guide, open benchmarks, all scripts on GitHub

**Say:**
"The target was no increase in latency. With all three fixes, the fully hybrid connection takes 14.5 ms instead of
21 for the old legacy one: 31 % faster. Against the hackathon server, 7 % faster. We tested 8,000 connections with
zero failures, and the cost is one degree more at idle and a few kilobytes per handshake. And because every step is
a setting, a compiler flag or a small code change, the same playbook works for other embedded Linux devices.
That's what our migration guide and our company are about."

---

### Tips
- **Total ≈ 2 min**, leaving 1–2 min for the problem and the company. If time is short, drop slide 2's table and keep
  only the chart.
- **Show few numbers:** 28 → 14.5 ms and "−31 %" are the ones to remember.
- **If asked about limits:** FOCAL's server supports the hybrid key exchange (we used it), but its certificate is
  RSA-3072 only and it refuses ECDSA and ML-DSA signatures, so hybrid authentication was measured against our own
  server, with the same setup for legacy and hybrid. The ML-DSA signature uses a custom TLS extension; the production
  path is standard ML-DSA or composite certificates. Energy wasn't measured; temperature is the proxy.
- More detail and practice questions: `achieved_results/study_guide/`.
