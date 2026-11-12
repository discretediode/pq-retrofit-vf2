# Handoff: PQ Retrofit on VisionFive 2

Paste this file into Claude (or open this repo in Claude Code) to get up to speed.

- **Setup:** `BOARD_SETUP_GUIDE_HAILO.md` steps 1–4. **Skip steps 5–9 (Hailo): we have no Hailo.**
- **Testing:** sections 4 and 5 below.
- **Recording results:** section 6 below.

---

## 1. The challenge

NextSemi / SEMICON FOCAL hackathon, "Post-Quantum Retrofit".

- **Task:** replace RSA authentication in Mbed TLS with **hybrid ML-DSA + ECDSA**. A message is accepted only if **both** signatures verify.
- **Hard constraint:** **no increase in end-to-end handshake latency** compared with RSA.
- **Optional:** hybrid ML-KEM key exchange; hardware acceleration.
- **Scoring (/30):** hybrid-auth result 10 (counts double); feasibility 5; innovation 5; market 5; business 5.
- **Mentor's hint:** it's solvable without added latency by "modifying the C library" and using the hardware creatively.

## 2. Hardware (confirmed on our board)

- **Board:** StarFive VisionFive 2 with the JH7110 chip: **4× SiFive U74 cores**, Debian.
- **ISA string:** `rv64imafdc_zicntr_zicsr_zifencei_zihpm_zca_zcd_zba_zbb`
  - **No `v`**: no vector unit.
  - **`zbb`**: rotate in a single instruction. Keccak/SHAKE is mostly rotations.
  - **`zicntr`/`zihpm`**: cycle counters and hardware performance counters, so `perf` works.
- **Compiler:** gcc on the board accepts `-march=rv64gc_zba_zbb` (checked).
- **No Hailo / NPU.** `hailo/`, `src/accel*.{c,h}`, `src/test_accel.c` and the patch are **parked**. Don't spend time on them.
- **Folder layout on the board:** everything sits next to each other in `~/hack/`:
  - `~/hack/pq-retrofit-vf2` (this repo)
  - `~/hack/dilithium` (at commit `d35ba3f`)
  - `~/hack/mbedtls-3.6.2`

## 3. Strategy and work plan

ML-DSA time is mostly **Keccak/SHAKE** (expanding the matrix `A`, sampling `y`, the challenge), with the **NTT** second. ML-DSA-44 signing is a retry loop that averages about 4 rounds. So:
1. **hash less**: cache the expanded key;
2. **hash faster**: Zbb rotates;
3. **retry in parallel** on 4 cores.

Rule: every optimisation must keep the output **byte-identical** to the reference ML-DSA. The golden test (T2) proves this, so we can say "fully standard FIPS 204".

| # | Step | Status | Test |
|---|---|---|---|
| P1 | Baseline + profile | To do | T1–T5 |
| P2 | Zbb compile flag (`make MARCH=rv64gc_zba_zbb`) | Makefile done, not measured | T6 |
| P3 | Cache the expanded key | **Not written** | T7 |
| P4 | Parallel signing rounds | **Not written** | T8 |
| P5 | ECDSA tuning (Mbed TLS config) | Only if ECDSA becomes the slower half | T5 rerun |
| P6 | TLS integration | Not written | T9 |
| P7 | Slides | — | Section 6 |

**P3: cache the expanded key.**
- The ref `sign.c` re-runs `polyvec_matrix_expand` (SHAKE-heavy) and NTTs `s1`/`s2`/`t0` on **every** call.
- Split out a "prepare key" step that runs once per key.
- Keep `crypto_sign_signature` / `crypto_sign_verify` working as before, because the golden test calls them.

**P4: parallel signing rounds.**
- Each round κ (`nonce` in ref `sign.c`) depends only on its own number.
- Run rounds κ, κ+1, κ+2 on 3 cores, with ECDSA on the 4th. Output the **lowest-numbered round that passes**. That gives exactly the bytes sequential code would produce.
- Use persistent worker threads, not one `pthread_create` per call.
- Signing only; it does not speed up verification.

**P5: ECDSA tuning.** In `mbedtls-3.6.2/include/mbedtls/mbedtls_config.h`:
- `MBEDTLS_ECP_FIXED_POINT_OPTIM`
- a bigger `MBEDTLS_ECP_WINDOW_SIZE`

**P6: TLS integration.** The functions to change are `ssl_tls13_write_certificate_verify_body` and `ssl_tls13_parse_certificate_verify` in `library/ssl_tls13_generic.c` (BOARD_SETUP_GUIDE_HAILO.md step 10).

---

## 4. Testing ground rules (read before measuring anything)

1. **Lock the CPU speed** at the start of every session (BOARD_SETUP_GUIDE_HAILO.md step 3). Without this, numbers can swing by 2×.
   ```bash
   echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
   ```
2. **Nothing else running.** Close other SSH sessions that are compiling. Don't benchmark while someone else is building on the board.
3. **Run every benchmark 3 times.** Name the files `<tag>_run1.csv`, `<tag>_run2.csv`, `<tag>_run3.csv`.
   - Report the **middle** of the three medians.
   - If the three medians differ by more than **5%**, something is disturbing the board. Find it and rerun.
4. **Same settings for every comparison:** same `runs` count (200), same keys, same governor. Change one thing at a time.
5. **Correctness before speed.** A config that fails T2 has no valid speed result. Don't record it.
6. **Write down the environment** once per session (T1). Numbers without it can't be trusted later.
7. **Commit results immediately**, with the commit message format in section 6.

All commands below run **on the board** from `~/hack/pq-retrofit-vf2`, unless they say otherwise.

## 5. Test catalogue

### T1: Record the environment (once per session, ~1 min)

Change `tag=` to the config you're about to test:
```bash
tag=baseline; { date; git rev-parse --short HEAD; git status --short | head; uname -a; gcc --version | head -1; grep -m1 isa /proc/cpuinfo; cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor | sort | uniq -c; cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq; } > results/env_$tag.txt; cat results/env_$tag.txt
```
- **Pass:** all 4 governors say `performance`.
- `git status` should be empty, or every change should be one you expect.

### T2: Golden test, i.e. "output is byte-identical to standard ML-DSA" (~2 min)

`make check` builds the official test-vector program, which signs 10,000 messages with fixed random seeds, and saves the output.

**Do this ONCE, on unmodified code, before any optimisation.** It creates the reference fingerprint:
```bash
make clean && make check && sha256sum bin/vectors_ref.txt | cut -d' ' -f1 > results/vectors_golden.sha256 && cat results/vectors_golden.sha256
```
Commit `results/vectors_golden.sha256` straight away.

**After every change**, rebuild with the same flags you'll benchmark with, then compare. Add `MARCH=...` if testing Zbb, plus any `-D` flag your change needs:
```bash
make clean && make check MARCH=rv64gc_zba_zbb && [ "$(sha256sum bin/vectors_ref.txt | cut -d' ' -f1)" = "$(cat results/vectors_golden.sha256)" ] && echo "GOLDEN MATCH"
```
- **Pass:** you see both `CHECK PASSED` and `GOLDEN MATCH`.
- **Fail:** the change altered the signatures. It is no longer standard ML-DSA. Fix it before measuring anything.
- **Proves:** the optimisation changes speed only, never the bytes. Our 10,000 vectors include signatures that needed many rounds, so the retry-loop logic is covered.
- For P4 (threads), run this **3 times in a row**. A race condition can pass once by luck.

### T3: Build and self-check (~2 min)

```bash
make clean && make && mkdir -p keys && [ -f keys/mldsa_pk.bin ] || ./bin/keytool gen keys
```
- Generate keys **once** and keep using them. Keys are git-ignored, so each board makes its own.
- `bench` checks itself when it starts. It aborts with `corrupted ... signature was ACCEPTED` or `hybrid self-check failed` if the hybrid check is broken.
- **Pass:** the CSV header `op,backend,median_us,...` is printed when you run T4.

### T4: Benchmark one config (~5 min per run)

Use the same tag as T1:
```bash
tag=baseline; for i in 1 2 3; do ./bin/bench_ref keys 200 > results/${tag}_run$i.csv; done; for i in 1 2 3; do column -s, -t results/${tag}_run$i.csv; echo; done
```
- Read the `median_us` column. `min_us`/`max_us` show how noisy the run was.
- **Pass:** the medians of the 3 runs agree within 5% (rule 3).
- Tags to use: `baseline`, `zbb`, `zbb_cache`, `zbb_cache_par`, `ecdsa_tuned`.

### T5: Profile, i.e. where does the time go? (~5 min, once for the baseline and once after P3)

```bash
sudo apt install -y linux-perf && sudo sysctl kernel.perf_event_paranoid=1
```
```bash
tag=baseline; perf record -g -o results/perf_$tag.data ./bin/bench_ref keys 50 > /dev/null && perf report -i results/perf_$tag.data --stdio --no-children | grep -E '^ +[0-9]' | head -25 > results/profile_$tag.txt; cat results/profile_$tag.txt
```
- Write the top 5 functions and their % in the results log.
- We expect `KeccakF1600_StatePermute` near the top, plus `ntt`/`invntt_tomont`. If that's wrong, the plan order changes. Tell the team.
- Commit `profile_*.txt` only. The `perf_*.data` files are big.

### T6: Zbb build (P2)

```bash
make clean && make MARCH=rv64gc_zba_zbb && objdump -d bin/bench_ref | grep -cw ror
```
- **Pass:** the `ror` count is well above 0. That proves the compiler used the rotate instruction.
- If the count is 0, the Keccak rotate macro wasn't recognised. Write the rotate as `(a << n) | (a >> (64 - n))` in `fips202.c`, or ask Claude.
- Then run T2 (built with `MARCH=...`) and T4 with `tag=zbb`.
- Optional extra: rebuild Mbed TLS with the flag too, then redo T4 as `tag=zbb_mbedtls`:
  ```bash
  make -C ../mbedtls-3.6.2/library clean && make -C ../mbedtls-3.6.2/library -j4 CFLAGS="-O2 -march=rv64gc_zba_zbb" libmbedcrypto.a
  ```

### T7: Expanded-key cache (P3)

- T2 → **GOLDEN MATCH** required.
- T4 with `tag=zbb_cache`.
- T5 with `tag=zbb_cache`. The share of time spent on SHAKE for matrix `A` should mostly disappear.
- Note in the log that the one-time "prepare key" cost is **excluded** from the per-signature time. Measure it separately (one call, timed) and report it as setup cost.

### T8: Parallel signing rounds (P4)

- T2 **three times** → GOLDEN MATCH every time.
- T4 with `tag=zbb_cache_par`, then once more with 1000 runs as a stress test:
  ```bash
  ./bin/bench_ref keys 1000 > results/zbb_cache_par_stress.csv
  ```
- Watch `max_us`. A huge max next to a normal median means a thread waited on another (scheduling hiccups). Note it in the log.
- Check all 4 cores are busy while it runs: open a second SSH session and run `top`, then press `1`.

### T9: TLS handshake smoke test (P6)

Build Mbed TLS's example TLS programs once:
```bash
make -C ../mbedtls-3.6.2 -j4 programs
```
Then use two SSH sessions on the board. In session 1, start the server:
```bash
../mbedtls-3.6.2/programs/ssl/ssl_server2 force_version=tls13
```
In session 2, run the client:
```bash
../mbedtls-3.6.2/programs/ssl/ssl_client2 server_name=localhost force_version=tls13
```
- **Pass:** the client log shows the handshake finishing (`[ Protocol is TLSv1.3 ]`, then data exchanged).
- Run it once **before** P6 (RSA) and once **after** (hybrid).
- Option names can differ slightly between versions. Run `ssl_client2 help` if one is rejected.
- **Not written yet:** a proper handshake *timing* harness (many handshakes, median). Until that exists, the end-to-end claim rests on the sign + verify numbers from T4 (section 6.3). Say so on the slides.

---

## 6. Result documentation

### 6.1 Files in `results/`

| File | Made by | Commit? |
|---|---|---|
| `vectors_golden.sha256` | T2 (once) | **Yes** (reference fingerprint) |
| `env_<tag>.txt` | T1 | Yes |
| `<tag>_run{1,2,3}.csv` | T4 | Yes |
| `profile_<tag>.txt` | T5 | Yes |
| `perf_<tag>.data` | T5 | **No** (too large) |
| `*_stress.csv` | T8 | Yes |

### 6.2 What the CSV columns mean

`op,backend,median_us,min_us,max_us,sig_bytes`:
- **`median_us`**: the number we quote. Typical time per operation, in microseconds.
- **`min_us`/`max_us`**: best and worst runs. Only used to judge noise.
- **`sig_bytes`**: signature size. Hybrid = ECDSA (~71 B) + ML-DSA-44 (2420 B).
- `rsa2048_sign` runs only `runs/10` times because it is slow. That's expected.

### 6.3 The verdict formula ("no latency increase?")

In TLS 1.3 server authentication, the server **signs** once and the client **verifies** once. So:

```
auth_cost_RSA    = rsa2048_sign.median    + rsa2048_verify.median
auth_cost_hybrid = hybrid_sign_par.median + hybrid_verify_par.median
```

- **Verdict: PASS** if `auth_cost_hybrid ≤ auth_cost_RSA`.
- Also report the **margin**: `(auth_cost_RSA − auth_cost_hybrid) / auth_cost_RSA` as a %.
- The `_seq` rows show how much the parallel hybrid saves. Put them on the slide too.
- **Caveat to state honestly:** the hybrid sends about 2.4 KB more signature data, plus a larger certificate. On a LAN that costs microseconds. On slow links, network time could exceed the CPU savings.

### 6.4 Results log (fill in; one row per config, using the middle run of 3)

| Date | Who | Commit | Tag | Flags | rsa sign | rsa verify | hyb sign par | hyb verify par | Auth RSA | Auth hybrid | Margin | Golden | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| | | | baseline | — | | | | | | | | ✅/❌ | |
| | | | zbb | MARCH | | | | | | | | | |
| | | | zbb_cache | MARCH | | | | | | | | | |
| | | | zbb_cache_par | MARCH | | | | | | | | | |

All times are median µs. Also keep this table of single operations (median µs), which the slides use:

| Tag | ecdsa sign | ecdsa verify | mldsa44 sign | mldsa44 verify | hyb sign seq | hyb verify seq |
|---|---|---|---|---|---|---|
| baseline | | | | | | |
| zbb | | | | | | |
| zbb_cache | | | | | | |
| zbb_cache_par | | | | | | |

Profile log, top 5 functions:

| Tag | #1 | #2 | #3 | #4 | #5 |
|---|---|---|---|---|---|
| baseline | | | | | |
| zbb_cache | | | | | |

### 6.5 Commit message format

Format: `results: <tag> — auth hybrid <X> µs vs RSA <Y> µs (<margin>%), golden OK`

```bash
git add results/ HANDOFF.md && git commit -m "results: baseline — auth hybrid X us vs RSA Y us, golden OK" && git push
```

Pull before you start (`git pull`), so two people don't log the same tag.

### 6.6 What goes on the slides

1. **Bar chart, auth cost per config:** RSA, hybrid seq, hybrid par, +Zbb, +cache, +parallel rounds. Draw the RSA line across as the target.
2. **Profile pie chart, before and after:** "we removed the hashing hotspot".
3. **Correctness badge:** "10,000 official test vectors byte-identical (SHA-256 `<first 8 hex>`)", from T2.
4. **Size table:** RSA 256 B vs hybrid ~2.5 KB signature. Note this honestly as the cost.

---

## 7. Files

| Path | What it is | Status |
|---|---|---|
| `src/bench.c` | Benchmarks RSA / ECDSA / ML-DSA / hybrid (seq + 2-core par); CSV; rejects corrupted signatures | Not compiled yet |
| `src/keytool.c` | ML-DSA keygen (`gen keys`) | Not compiled yet |
| `Makefile` | `make`, `make check`, `make MARCH=...` | Not run yet |
| `results/` | All measurements (section 6) | Empty |
| `src/accel*.{c,h}`, `src/test_accel.c`, `patches/`, `hailo/` | The old Hailo plan | **Parked**. The patch is harmless: it does nothing without `-DACCEL` |

## 8. Ideas for the slides (not building)

- **Session resumption / cached cert verification:** "PQC costs once per client, not per connection."
- **Client pre-generates its ML-KEM key share** before connecting.
- **Verify the cert chain and CertificateVerify on separate cores.**
- **Keep the first flight under ~14 KB.** The TCP initial window is about that size, and going over costs an extra round trip.
- **Offline/online signing:** precompute `(y, w)` pairs while idle. ⚠️ **Not FIPS 204.** Pitch only.
- **KEMTLS:** the server authenticates with a KEM instead of a signature. Roadmap only.

## 9. Gotchas and troubleshooting

| Symptom | Fix |
|---|---|
| Numbers jump around | Governor (rule 1). Check nobody else is compiling. Use 3 runs. |
| `cannot read keys/mldsa_pk.bin` | `./bin/keytool gen keys` |
| `GOLDEN MATCH` doesn't print | Your change altered the signatures. Undo it, or debug it with Claude. Don't record speed numbers. |
| Golden fails only sometimes (P4) | A race condition between threads. Check that every thread uses its own buffers and that the output is the *lowest* passing round. |
| `-march` error on a Mac | Don't pass `MARCH=` on a Mac; it's RISC-V only. |
| `~` paths fail in make on a Mac | Use `$HOME`: `make check DILITHIUM=$HOME/hack/dilithium/ref MBEDTLS=$HOME/hack/mbedtls-3.6.2` |
| `perf: command not found` / permission denied | Run the T5 install line. |
| `git apply` fails | `cd ~/hack/dilithium && git checkout d35ba3f` |

**Open question:** in the demo, does the board **sign** (server), **verify** (client), or both? P4 only helps signing. Ask the organisers.
