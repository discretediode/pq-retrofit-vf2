# Source code (OpenSSL 3.5)

C programs used for every measurement in [`../results/`](../results/). They need OpenSSL 3.5 or newer
(ML-KEM and ML-DSA built in).

| File | Runs on | What it does |
|---|---|---|
| `hybrid_server.c` | Mac (test server) | TLS 1.3 server: RSA-3072 certificate for legacy clients; ECDSA P-256 certificate + ML-DSA-44/65 signature for hybrid clients (v1: signature over the TLS exporter; v2: signature in a Certificate extension `0xFE20` over the hello messages) |
| `hybrid_client.c` | Board | Benchmark client: pinned certificate and ML-DSA key, end-to-end timing. Options `certext` (v2) and `par` (verify ML-DSA on a second core). `RAW=1` prints every connection, `TRACE=1` prints the handshake timeline |
| `pqc_client.c` | Board | Key-exchange client for FOCAL's hackathon server; selectable group (`X25519` or `X25519MLKEM768`) |
| `sigbench.c` | Board | Signature micro-benchmark: RSA-3072, ECDSA P-256, ML-DSA-44/65 sign and verify |
| `parverify.c` | Board | Multicore feasibility test: classical and post-quantum halves one after another vs on two cores |

## Build

On the board, with OpenSSL installed in `$P` (e.g. `/opt/openssl-3.5`):

```bash
P=/opt/openssl-3.5
gcc -O2 -Wall hybrid_client.c -o hybrid_client -I$P/include -L$P/lib -Wl,-rpath,$P/lib -lssl -lcrypto -lpthread
gcc -O2 -Wall pqc_client.c    -o pqc_client    -I$P/include -L$P/lib -Wl,-rpath,$P/lib -lssl -lcrypto
gcc -O2 -Wall sigbench.c      -o sigbench      -I$P/include -L$P/lib -Wl,-rpath,$P/lib -lcrypto
gcc -O2 -Wall parverify.c     -o parverify     -I$P/include -L$P/lib -Wl,-rpath,$P/lib -lcrypto -lpthread
```

On the Mac (Homebrew OpenSSL 3.5+): `cc -O2 -Wall hybrid_server.c -o hybrid_server -I$P/include -L$P/lib -lssl -lcrypto`.

The three OpenSSL builds used on the board (plain, `enable-ec_nistp_64_gcc_128`, and that plus
`-march=rv64gc_zba_zbb`) are described in [`../docs/PROJECT_SUMMARY.md`](../docs/PROJECT_SUMMARY.md) and
[`../results/03_bitmanip_zba_zbb/`](../results/03_bitmanip_zba_zbb/). Usage examples: `PROJECT_SUMMARY.md` section 9.
Keys and certificates are not in the repository.
