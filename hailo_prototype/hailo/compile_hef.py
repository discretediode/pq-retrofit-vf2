"""Compile sign.onnx / verify.onnx into a .hef for the Hailo-8L.
Runs ONLY on the x86 Linux machine with Hailo's Dataflow Compiler installed.

  python3 compile_hef.py sign     (needs sign.onnx + calib_sign.npy in this folder)
  python3 compile_hef.py verify

Written against the DFC Python API (ClientRunner). NOT tested here (no DFC on
a Mac). If a call fails, check the DFC docs for your version or ask the
technical advisor; the steps (translate -> optimize -> compile) stay the same.
"""
import sys
import numpy as np
from hailo_sdk_client import ClientRunner

kind = sys.argv[1] if len(sys.argv) > 1 else "sign"
runner = ClientRunner(hw_arch="hailo8l")
runner.translate_onnx_model(f"{kind}.onnx", kind)

layers = runner.get_hn_dict()["layers"]
print("layers:", {name: l["type"] for name, l in layers.items()})

# 16-bit weights + activations on the compute layer, and no fancy optimisation
# (which could nudge weights away from exact integers).
compute = [n for n, l in layers.items() if l["type"] in ("dense", "conv", "matmul")]
script = "model_optimization_flavor(optimization_level=0)\n"
script += "".join(f"quantization_param({n.split('/')[-1]}, precision_mode=a16_w16)\n" for n in compute)
print("model script:\n" + script)
runner.load_model_script(script)

# Shape the calibration data the way the parsed input layer expects it.
inp = next(l for l in layers.values() if l["type"] == "input_layer")
shape = inp["input_shapes"][0][1:]
calib = np.load(f"calib_{kind}.npy").reshape(-1, *shape)
print("calibration:", calib.shape)
runner.optimize(calib)

hef = runner.compile()
with open(f"{kind}.hef", "wb") as f:
    f.write(hef)
print(f"wrote {kind}.hef")
