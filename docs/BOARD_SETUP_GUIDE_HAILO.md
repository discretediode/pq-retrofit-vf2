# Board setup guide: Hailo-8L acceleration prototype

> This was the repository's original README. It describes the Mbed TLS + Hailo-8L prototype in `src/`, `hailo/`
> and `patches/`. The measured results use OpenSSL 3.5.9 without Hailo; see the [main README](../README.md).

Hybrid ML-DSA + ECDSA authentication on the StarFive VisionFive 2, with the
small ML-DSA multiplications offloaded to a Hailo-8L. FOCAL "Post-Quantum
Retrofit" hackathon.

**Design**

```
             ┌─ CPU core 1: ECDSA P-256 ─────────────────────┐
handshake ───┤                                                ├─ both valid? → accept
             └─ CPU core 2: ML-DSA-44 ── Hailo: c·s1, c·s2    ┘
                                         (or c·t1 when verifying)
```

---

## The step-by-step guide

Every command block is marked with **where** you type it:

| Mark | Where |
|---|---|
| 💻 **MAC** | Terminal app on your Mac |
| 🟢 **BOARD** | On the VisionFive 2, after `ssh` from your Mac |
| 🐧 **X86** | The x86 Linux machine that has Hailo's Dataflow Compiler |

Replace things in `<angle brackets>` with your own values, for example
`<board-ip>` → `192.168.1.42`.

**Who does what (3 people)**

| Person | Steps |
|---|---|
| **A** (integration) | 1 → 4, then step 10 (plug into the TLS handshake) |
| **B** (measurement) | runs every benchmark, fills the results table (step 11) |
| **C** (Hailo) | 5 → 9 |

A and C can start in parallel once step 2 is done.

---

### Step 0 — Put this code on GitHub (once, one person)

1. On github.com, click **New repository**, name it `pq-retrofit-vf2`, keep it **Private**, and do **not** add a README.
2. Then:

💻 **MAC**
```bash
cd <path-to>/pq-retrofit-vf2
git init -b main
git add .
git commit -m "Hybrid auth + Hailo offload skeleton"
git remote add origin https://github.com/<your-github-user>/pq-retrofit-vf2.git
git push -u origin main
```

---

### Step 1 — Set up the board (Person A, ~15 min)

💻 **MAC** — log into the board (the organisers give you the IP, user and password):
```bash
ssh <user>@<board-ip>
```

🟢 **BOARD** — install tools and download everything into `~/hack`:
```bash
sudo apt update && sudo apt install -y build-essential git wget bzip2
git config --global user.name "<your name>"
git config --global user.email "<your github email>"
mkdir -p ~/hack && cd ~/hack

# our code
git clone https://github.com/<your-github-user>/pq-retrofit-vf2.git

# ML-DSA reference code, pinned to the version the patch was made for
git clone https://github.com/pq-crystals/dilithium.git
cd dilithium && git checkout d35ba3f
git apply ../pq-retrofit-vf2/patches/dilithium-accel.patch
cd ..

# Mbed TLS 3.6.2 (only the crypto library is needed for the benchmarks)
wget https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2
tar xjf mbedtls-3.6.2.tar.bz2
make -C mbedtls-3.6.2/library -j4 libmbedcrypto.a
```

> The repo is private, so `git clone` asks for a login. Use your GitHub
> username and a **personal access token** as the password
> (GitHub → Settings → Developer settings → Personal access tokens).

> If the organisers' starter project already contains Mbed TLS, you can point
> at theirs instead: add `MBEDTLS=<path-to-their-mbedtls>` to every `make` below.

---

### Step 2 — Build and prove the patch is correct (Person A, ~5 min)

🟢 **BOARD**
```bash
cd ~/hack/pq-retrofit-vf2
make check
```

✅ You must see: `CHECK PASSED: patched ML-DSA matches reference`

This runs the official ML-DSA test vectors twice — normal, and through our
accelerator hook — and checks the output is byte-for-byte identical.
**If it fails, stop and fix this first. Everything else depends on it.**

Then build everything and create a test key:

🟢 **BOARD**
```bash
make
mkdir -p keys
./bin/keytool gen keys
```

---

### Step 3 — Lock the CPU speed (Person B, before ANY measurement)

🟢 **BOARD**
```bash
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
```
Write the frequency down. Do this again after every reboot.

---

### Step 4 — CPU-only benchmark: the guaranteed result (Persons A + B)

🟢 **BOARD**
```bash
./bin/bench_ref keys 200 | tee results/bench_ref.csv
```

You get a table like this (numbers are made up — yours will differ):
```
op,backend,median_us,min_us,max_us,sig_bytes
rsa2048_sign,ref,....
rsa2048_verify,ref,....
ecdsa_p256_sign,ref,....
...
hybrid_sign_seq,ref,....     ← ECDSA, then ML-DSA
hybrid_sign_par,ref,....     ← both at the same time on 2 cores
hybrid_verify_seq,ref,....
hybrid_verify_par,ref,....
```

How to read it:
- **`_par` vs `_seq`** — how much running on two cores saves.
- **`hybrid_*_par` vs `rsa2048_*`** — your headline: is hybrid as fast as RSA?
  Compare `sign` if the board signs in the handshake, `verify` if it only verifies.
- **`sig_bytes`** — the extra data sent over the network (RSA 256 B vs hybrid ~2,490 B).

Commit the result:

🟢 **BOARD**
```bash
git add results/bench_ref.csv && git commit -m "CPU baseline benchmark" && git push
```

---

### Step 5 — Check the Hailo is alive (Person C, ~10 min, TIME-BOX)

🟢 **BOARD**
```bash
lspci | grep -i hailo
hailortcli fw-control identify
ls /usr/include/hailo/hailort.h
```

✅ All three must work (a Hailo line, firmware info, the header file exists).

❌ If not by lunch: ask the organisers once. If still nothing, **stop the Hailo
track**, write down exactly what failed (that's a valid finding for the
migration guide), and help with step 10 / the slides.

---

### Step 6 — Turn the key into Hailo weights (Person C, ~10 min)

🟢 **BOARD**
```bash
cd ~/hack/pq-retrofit-vf2
./bin/keytool sign-weights   keys/mldsa_sk.bin keys/sign_weights.bin
./bin/keytool verify-weights keys/mldsa_pk.bin keys/verify_weights.bin
```

💻 **MAC** — copy them to your Mac (run inside your local `pq-retrofit-vf2` folder):
```bash
scp <user>@<board-ip>:~/hack/pq-retrofit-vf2/keys/*_weights.bin hailo/
```

> `sign_weights.bin` is derived from the **secret** key. It's a test key, so
> it's fine — but that's why `keys/` and `*_weights.bin` are in `.gitignore`.
> Mention in your presentation that signing on the Hailo means the secret key
> lives inside the accelerator's model file.

**Which model do you need?** Ask the organisers (or check the starter code)
whether the board **signs** or only **verifies** in the handshake.
- Board signs → build the **sign** model.
- Board only verifies → build the **verify** model.
- Not sure yet → build **sign** first (its numbers are smallest, so it's the
  easiest exactness test).

---

### Step 7 — Build the model on your Mac (Person C, ~10 min)

💻 **MAC**
```bash
cd hailo
python3 -m venv .venv
source .venv/bin/activate
pip install numpy onnx

python3 make_model.py selftest
python3 make_model.py sign   sign_weights.bin   .
python3 make_model.py verify verify_weights.bin .
```

✅ Expected output:
```
selftest OK: matrix trick and t1 split are exact
wrote ./sign.onnx (W (2048, 256), weights -2..2)
wrote ./calib_sign.npy (5120 samples, outputs -78..78)
...
```

What the files are:
- `sign.onnx` — a one-layer "AI model". Its weights are your key, arranged so
  that running it on a challenge gives exactly ML-DSA's `c·s1` and `c·s2`.
- `calib_sign.npy` — sample inputs the Hailo compiler uses to choose its
  number ranges. It includes the worst-case challenges so nothing gets clipped.

---

### Step 8 — Compile for the Hailo on the x86 machine (Person C, ~20 min)

💻 **MAC** — copy the files over:
```bash
ssh <x86-user>@<x86-ip> mkdir -p hailo-build
scp sign.onnx calib_sign.npy compile_hef.py <x86-user>@<x86-ip>:~/hailo-build/
```

🐧 **X86**
```bash
cd ~/hailo-build
source <path-to-dfc-venv>/bin/activate     # wherever the Dataflow Compiler is installed
python3 compile_hef.py sign
```

✅ Ends with `wrote sign.hef`.

> `compile_hef.py` could not be tested without the Hailo compiler. If a step
> errors, the message usually says what it wants (a layer name, a shape).
> Show it to the technical advisor — the flow (translate → optimize → compile)
> is standard.

Copy the result to the board:

🐧 **X86**
```bash
scp sign.hef <user>@<board-ip>:~/hack/pq-retrofit-vf2/hailo/
```
(If the x86 machine can't reach the board, copy it to your Mac first, then
from the Mac to the board.)

---

### Step 9 — Run ML-DSA on the Hailo and measure (Persons C + B)

🟢 **BOARD** — rebuild with Hailo support:
```bash
cd ~/hack/pq-retrofit-vf2
make clean && make HAILO=1
```

> `accel_hailo.c` could not be compiled on a Mac (no HailoRT there). If the
> compiler complains about a `hailo_...` function, compare with HailoRT's own
> example `vstreams_example.c` (in the HailoRT GitHub repo, `hailort/libhailort/examples/c/`)
> — the order of calls is the same.

**9a. Is it exact?** — the make-or-break test:

🟢 **BOARD**
```bash
ACCEL_SIGN_HEF=hailo/sign.hef ./bin/test_accel keys 1000 | tee results/test_hailo_sign.txt
```

✅ Look for:
```
backend: hailo-sign
setup_us: ...                          ← one-time cost to load the model
wrong_values: 0 of 2048000  (exact)    ← MUST be 0
accel_call_median_us: ...              ← Hailo, per signature attempt
cpu_ntt_path_median_us: ...            ← the same maths on the CPU
sign/verify roundtrip: OK ...
```

- `wrong_values: 0` → 🎉 the Hailo computes ML-DSA maths exactly. Continue.
- `wrong_values` > 0 → the quantization isn't exact. **That's your finding**:
  record the number, try once more with the advisor, then stop and write it up.

(For the verify model: `ACCEL_VERIFY_HEF=hailo/verify.hef ./bin/test_accel keys 1000`.)

**9b. Full benchmark with the Hailo:**

🟢 **BOARD**
```bash
ACCEL_SIGN_HEF=hailo/sign.hef ./bin/bench_accel keys 200 | tee results/bench_hailo_sign.csv
```

**9c. Throughput when the Hailo is kept busy (for the break-even analysis):**

🟢 **BOARD**
```bash
hailortcli run hailo/sign.hef | tee results/hailortcli_sign.txt
```
This prints frames per second when many challenges are streamed back-to-back.
`1,000,000 / FPS` = microseconds per multiplication **in bulk**. Compare it with
`accel_call_median_us` (one at a time) and `cpu_ntt_path_median_us` (CPU).

Commit everything:

🟢 **BOARD**
```bash
git add results/ && git commit -m "Hailo results" && git push
```

---

### Step 10 — Plug the hybrid check into the TLS handshake (Person A)

The benchmark proves the crypto. The core deliverable is a **working
handshake**, so the same hybrid logic has to go into the organisers' Mbed TLS
project.

**Find where the board checks / creates signatures:**

🟢 **BOARD** (inside the organisers' Mbed TLS folder)
```bash
grep -rn "mbedtls_pk_verify\|mbedtls_pk_sign" library/ssl_*.c
```
For TLS 1.3 the important functions are in `library/ssl_tls13_generic.c`:
- **verifying** the server: `ssl_tls13_parse_certificate_verify`
- **signing** (only if the board authenticates itself): `ssl_tls13_write_certificate_verify_body`

**What to change there:** where it calls `mbedtls_pk_verify...` for the
server's RSA signature, do the hybrid check instead — ECDSA via the existing
`mbedtls_pk_verify...` call, ML-DSA via `crypto_sign_verify(...)`, running at
the same time exactly like `run_par()` in `src/bench.c`, and accept only if
**both** pass.

How the two signatures and keys are carried in the handshake (one combined
certificate or two separate ones) depends on the organisers' test server —
**follow their starter project / porting list here**, and ask the mentors.

**If their project already has its own ML-DSA code**, apply our hook there
instead: search it for the same lines our patch changes,
```bash
grep -rn "pointwise_poly_montgomery" .
```
and use `patches/dilithium-accel.patch` as the template.

**Plan B (if full integration is stuck by lunch):** keep the normal TLS
connection and add one extra round-trip on top: the server signs a random
challenge with ECDSA + ML-DSA and the board verifies both. The brief accepts a
documented partial implementation.

---

### Step 11 — The results table (Person B)

Fill this in for the slides:

| | RSA baseline | Hybrid, CPU, sequential | Hybrid, CPU, parallel | Hybrid + Hailo |
|---|---|---|---|---|
| Handshake time (median, ms) | | | | |
| Sign or verify time (µs) | | | | |
| Signature + key bytes | 256 + 256 | | | |
| Peak memory (`/usr/bin/time -v`) | | | | |
| Energy (USB-C meter) | | | | |
| Hailo setup cost (µs) | – | – | – | |

Plus one chart: **time per multiplication vs. how many at once** — CPU (flat
line) against Hailo one-at-a-time and Hailo in bulk (step 9c). Where they cross
is the break-even point.

---

## What's in this repo

| File | What it does |
|---|---|
| `patches/dilithium-accel.patch` | Adds the hook to ML-DSA: with `-DACCEL`, `c·s1`, `c·s2` (signing) and `c·t1` (verifying) come from `accel_*()` instead of the NTT |
| `src/accel.c` / `accel.h` | The hook. CPU version always; Hailo when a `.hef` is set via env var |
| `src/accel_hailo.c` | Talks to the Hailo through HailoRT |
| `src/bench.c` | RSA vs ECDSA vs ML-DSA vs hybrid (sequential and parallel) → CSV |
| `src/test_accel.c` | Exactness check, timing, sign/verify roundtrip |
| `src/keytool.c` | Makes ML-DSA keys and exports the Hailo weights |
| `hailo/make_model.py` | Builds the one-layer ONNX model + calibration data |
| `hailo/compile_hef.py` | Compiles it to `.hef` (x86 Linux + Dataflow Compiler) |

**Tested so far:** `make_model.py` (the maths and the ONNX model are exact).
**Not yet compiled anywhere:** the C code and `compile_hef.py` — step 2
(`make check`) is the first real test.

## Troubleshooting

| Problem | Fix |
|---|---|
| `git apply` fails | You're not on commit `d35ba3f`: `cd ~/hack/dilithium && git checkout d35ba3f` |
| `mbedtls/... .h: No such file` | Mbed TLS isn't at `../mbedtls-3.6.2` — add `MBEDTLS=<path>` to `make` |
| `cannot read keys/mldsa_pk.bin` | Run `./bin/keytool gen keys` (step 2) |
| `hailo: ... failed, status N` | Look up the status number in `hailort.h`; most common = model/device busy (close other Hailo programs) |
| `hailo: input is X bytes, expected 1024` | The compiler changed the input shape — show the advisor the `layers:` line printed by `compile_hef.py` |
| Numbers jump around a lot | Step 3 (CPU governor), close other programs, use more runs |
