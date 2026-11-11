# Study guide: hybrid post-quantum TLS on the VisionFive 2

Preparation material for the team, to explain the project and answer the judges' questions.

- **[`study_guide.pdf`](study_guide.pdf)**: read it directly on GitHub. All practice answers are shown.
- **`study_guide.html`**: the interactive version. Download it (Raw → save) and open it in a browser. You can open
  the answer cards one by one, tick the ones you know, and show only the ones you don't know yet.

## What's inside, in study order
1. The 60-second pitch
2. Why post-quantum cryptography is needed (Shor, Grover, harvest now / decrypt later)
3. How a TLS 1.3 connection works (key exchange vs authentication), with a handshake diagram
4. The algorithms: X25519, ML-KEM-768, RSA-3072, ECDSA P-256, ML-DSA-44/65 (sizes and costs on the board)
5. Why hybrid instead of pure post-quantum
6. The board: JH7110, 4 × U74, no vector unit, Zba/Zbb, one shared clock
7. What was built: OpenSSL 3.5.9 builds, hybrid key exchange, hybrid authentication v1 and v2, pinned trust
8. How it was measured: 1000 runs, alternating blocks, medians, paired comparisons
9. The three speed-ups (min 750 MHz clock, Zba/Zbb build, second core) and how they add up
10. Final results, memory, bytes, temperature, energy, Hailo-8L
11. Limits to mention upfront
12. 35 practice questions with answers
13. Glossary

Every number comes from the reports in this folder; see [`../SUMMARY.md`](../SUMMARY.md) and
[`../combined_all_approaches/combined_REPORT.md`](../combined_all_approaches/combined_REPORT.md).
