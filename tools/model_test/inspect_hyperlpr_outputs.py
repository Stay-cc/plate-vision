#!/usr/bin/env python3
"""Inspect HyperLPR detector output layout with a zero input."""

from pathlib import Path
import sys

import numpy as np
import onnxruntime as ort

REPO_ROOT = Path(__file__).resolve().parents[2]
MODEL = (
    REPO_ROOT
    / "third_party"
    / "hyperlpr"
    / "models"
    / "20230229"
    / "onnx"
    / "y5fu_640x_sim.onnx"
)


def main() -> int:
    session = ort.InferenceSession(
        str(MODEL),
        providers=["CPUExecutionProvider"],
    )
    input_name = session.get_inputs()[0].name
    output = session.run(
        None,
        {input_name: np.zeros((1, 3, 640, 640), dtype=np.float32)},
    )[0]

    print("shape", output.shape, output.dtype)
    print("min", output.min(axis=(0, 1)))
    print("max", output.max(axis=(0, 1)))
    print("row0", output[0, 0])
    print("row19200", output[0, 19200])
    print("row24000", output[0, 24000])
    print("rows_with_objectness_gt_0_3", int((output[0, :, 4] > 0.3).sum()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

