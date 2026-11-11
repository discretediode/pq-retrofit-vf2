"""Build the one-layer ONNX model the Hailo runs, plus its calibration data.

  python3 make_model.py sign   keys/sign_weights.bin   out/   -> out/sign.onnx,   out/calib_sign.npy
  python3 make_model.py verify keys/verify_weights.bin out/   -> out/verify.onnx, out/calib_verify.npy
  python3 make_model.py selftest                               -> checks the maths, no files needed

The model is  y = W @ c  with W fixed (2048 x 256) and c the 256 challenge
coefficients. Each 256x256 block of W is the "negacyclic" matrix of one key
polynomial, so W @ c equals the ML-DSA polynomial products.
Output layout must match src/accel_hailo.c.
"""
import sys
import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

N, K, L, TAU = 256, 4, 4, 39  # ML-DSA-44


def negacyclic(s):
    """M such that M @ c == c*s in Z[x]/(x^256 + 1)."""
    i = np.arange(N)[:, None]
    j = np.arange(N)[None, :]
    k = i - j
    return np.where(k >= 0, s[k % N], -s[k % N]).astype(np.int64)


def polymul_ref(a, b):
    """Slow, obviously-correct reference."""
    out = np.zeros(N, dtype=np.int64)
    for x in range(N):
        for y in range(N):
            if x + y < N:
                out[x + y] += a[x] * b[y]
            else:
                out[x + y - N] -= a[x] * b[y]
    return out


def random_challenge(rng):
    """Like ML-DSA's poly_challenge: TAU entries of +-1, rest 0."""
    c = np.zeros(N, dtype=np.int64)
    pos = rng.choice(N, TAU, replace=False)
    c[pos] = rng.choice([-1, 1], TAU)
    return c


def build_weights(kind, path):
    if kind == "sign":
        s = np.fromfile(path, dtype=np.int8).astype(np.int64)
        assert s.size == (L + K) * N, f"expected {(L + K) * N} values, got {s.size}"
        assert np.abs(s).max() <= 2, "sign weights must be in [-2, 2]"
        polys = s.reshape(L + K, N)  # s1[0..3], s2[0..3]
    else:
        t1 = np.fromfile(path, dtype=np.int16).astype(np.int64)
        assert t1.size == K * N, f"expected {K * N} values, got {t1.size}"
        assert t1.min() >= 0 and t1.max() <= 1023, "t1 must be in [0, 1023]"
        t1 = t1.reshape(K, N)
        polys = np.concatenate([t1 % 32, t1 // 32])  # lo halves, then hi halves
    return np.vstack([negacyclic(p) for p in polys])  # (2048, 256)


def calibration(W, rng, n_random=1024):
    """Random challenges plus, for every output, the challenge that pushes it
    to its true max and min. Without these the quantizer may clip rare large
    outputs and the result would no longer be exact."""
    rows = [random_challenge(rng) for _ in range(n_random)]
    for r in range(W.shape[0]):
        top = np.argsort(-np.abs(W[r]))[:TAU]
        c = np.zeros(N, dtype=np.int64)
        c[top] = np.sign(W[r, top])
        c[top[W[r, top] == 0]] = 1
        rows += [c, -c]
    return np.array(rows, dtype=np.float32)


def save_onnx(W, path):
    # y[1,2048] = c[1,256] @ W.T[256,2048]  -> a single dense layer
    weight = numpy_helper.from_array(W.T.astype(np.float32), name="W")
    node = helper.make_node("MatMul", ["c", "W"], ["y"], name="dense")
    graph = helper.make_graph(
        [node], "mldsa_mul",
        [helper.make_tensor_value_info("c", TensorProto.FLOAT, [1, N])],
        [helper.make_tensor_value_info("y", TensorProto.FLOAT, [1, W.shape[0]])],
        initializer=[weight])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
    model.ir_version = 8  # readable by older ONNX/Hailo tools
    onnx.checker.check_model(model)
    onnx.save(model, path)


def selftest():
    rng = np.random.default_rng(0)
    # 1. matrix trick == polynomial multiplication
    for _ in range(5):
        s = rng.integers(-2, 3, N)
        c = random_challenge(rng)
        assert np.array_equal(negacyclic(s) @ c, polymul_ref(c, s))
    # 2. verify split: lo + 32*hi recombines to c*t1
    t1 = rng.integers(0, 1024, N)
    c = random_challenge(rng)
    lo, hi = negacyclic(t1 % 32) @ c, negacyclic(t1 // 32) @ c
    assert np.array_equal(lo + 32 * hi, polymul_ref(c, t1))
    assert max(np.abs(lo).max(), np.abs(hi).max()) <= TAU * 31
    print("selftest OK: matrix trick and t1 split are exact")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "selftest":
        return selftest()
    if len(sys.argv) != 4 or sys.argv[1] not in ("sign", "verify"):
        sys.exit(__doc__)
    kind, wpath, outdir = sys.argv[1:]
    W = build_weights(kind, wpath)
    save_onnx(W, f"{outdir}/{kind}.onnx")
    calib = calibration(W, np.random.default_rng(1))
    np.save(f"{outdir}/calib_{kind}.npy", calib)
    out = calib.astype(np.int64) @ W.T
    print(f"wrote {outdir}/{kind}.onnx (W {W.shape}, weights {W.min()}..{W.max()})")
    print(f"wrote {outdir}/calib_{kind}.npy ({len(calib)} samples, outputs {out.min()}..{out.max()})")


if __name__ == "__main__":
    main()
