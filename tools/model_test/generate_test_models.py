#!/usr/bin/env python3
"""Generate deterministic ONNX models used by the C++ integration tests."""

from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[2]
DEPS_DIR = REPO_ROOT / "third_party" / "pydeps"
if DEPS_DIR.exists():
    sys.path.insert(0, str(DEPS_DIR))

import numpy as np
import onnx
from onnx import TensorProto, helper


def load_symbols(path: Path) -> list[str]:
    symbols = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()]
    symbols = [symbol for symbol in symbols if symbol]
    if len(symbols) < 2 or symbols[0] != "<blank>":
        raise ValueError("alphabet must start with <blank>")
    return symbols


def save_model(model: onnx.ModelProto, path: Path) -> None:
    onnx.checker.check_model(model)
    path.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, path)
    print(f"wrote {path}")


def build_detector(detections_first: bool = False) -> onnx.ModelProto:
    output_shape = [1, 1, 5] if detections_first else [1, 5, 1]
    input_tensor = helper.make_tensor_value_info(
        "images", TensorProto.FLOAT, [1, 3, 640, 640]
    )
    output_tensor = helper.make_tensor_value_info(
        "detections", TensorProto.FLOAT, output_shape
    )

    # cx, cy, width, height, score in detector input pixels.
    output_value = helper.make_tensor(
        "detections",
        TensorProto.FLOAT,
        output_shape,
        np.asarray([320.0, 320.0, 200.0, 60.0, 0.95], dtype=np.float32),
    )
    graph = helper.make_graph(
        [],
        "constant_plate_detector",
        [input_tensor],
        [output_tensor],
        initializer=[output_value],
    )
    return helper.make_model(
        graph,
        producer_name="platevision-test-model-generator",
        opset_imports=[helper.make_opsetid("", 17)],
    )


def build_recognizer(
    symbols: list[str], plate: str, class_major: bool = False
) -> onnx.ModelProto:
    output_shape = (
        [1, len(symbols), len(plate)]
        if class_major
        else [1, len(plate), len(symbols)]
    )
    input_tensor = helper.make_tensor_value_info(
        "plate", TensorProto.FLOAT, [1, 3, 24, 96]
    )
    output_tensor = helper.make_tensor_value_info(
        "logits", TensorProto.FLOAT, output_shape
    )

    logits = np.zeros((1, len(plate), len(symbols)), dtype=np.float32)
    for step, character in enumerate(plate):
        try:
            token_index = symbols.index(character)
        except ValueError as error:
            raise ValueError(f"plate character missing from alphabet: {character}") from error
        logits[0, step, token_index] = 12.0

    stored_logits = logits.transpose(0, 2, 1) if class_major else logits
    output_value = helper.make_tensor(
        "logits",
        TensorProto.FLOAT,
        list(stored_logits.shape),
        stored_logits.ravel(),
    )
    graph = helper.make_graph(
        [],
        "constant_plate_recognizer",
        [input_tensor],
        [output_tensor],
        initializer=[output_value],
    )
    return helper.make_model(
        graph,
        producer_name="platevision-test-model-generator",
        opset_imports=[helper.make_opsetid("", 17)],
    )


def main() -> int:
    symbols = load_symbols(REPO_ROOT / "configs" / "plate_alphabet.txt")
    output_dir = REPO_ROOT / "tests" / "models"
    save_model(build_detector(), output_dir / "detector_constant.onnx")
    save_model(
        build_detector(detections_first=True),
        output_dir / "detector_detections_first.onnx",
    )
    save_model(
        build_recognizer(symbols, "京A12345"),
        output_dir / "recognizer_constant.onnx",
    )
    save_model(
        build_recognizer(symbols, "京A12345", class_major=True),
        output_dir / "recognizer_class_major.onnx",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

