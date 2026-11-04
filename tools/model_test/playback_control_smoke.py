#!/usr/bin/env python3
"""Verify pause/resume/speed control over plate_cli stdin."""

import json
import queue
import subprocess
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CLI = REPO_ROOT / "build" / "release" / "bin" / "Release" / "plate_cli.exe"
SOURCE = REPO_ROOT / "output" / "hyperlpr_pass.mp4"
TOKENS = REPO_ROOT / "configs" / "hyperlpr_tokens.txt"
MODELS = REPO_ROOT / "third_party" / "hyperlpr" / "models" / "20230229" / "onnx"


def main() -> int:
    command = [
        str(CLI),
        "--input",
        str(SOURCE),
        "--engine",
        "hyperlpr",
        "--ui-stream",
        "--control-stdin",
        "--no-records",
        "--detector-model",
        str(MODELS / "y5fu_640x_sim.onnx"),
        "--recognizer-model",
        str(MODELS / "rpv3_mdict_160_r3.onnx"),
        "--hyperlpr-tokens",
        str(TOKENS),
        "--queue-capacity",
        "8",
    ]
    process = subprocess.Popen(
        command,
        cwd=REPO_ROOT,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )
    messages: queue.Queue[dict] = queue.Queue()

    def reader() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            line = line.strip()
            if line:
                messages.put(json.loads(line))

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()

    status = messages.get(timeout=5)
    assert status.get("type") == "status"
    assert process.stdin is not None
    frames = 0
    while frames == 0:
        message = messages.get(timeout=5)
        if message.get("type") == "frame":
            frames += 1

    process.stdin.write("pause\n")
    process.stdin.flush()

    time.sleep(0.25)
    while True:
        try:
            message = messages.get_nowait()
        except queue.Empty:
            break
        if message.get("type") == "frame":
            frames += 1

    start = time.monotonic()
    while time.monotonic() - start < 0.7:
        try:
            message = messages.get(timeout=0.1)
        except queue.Empty:
            continue
        assert message.get("type") != "frame", "frame emitted while paused"

    process.stdin.write("speed 2.0\n")
    process.stdin.write("resume\n")
    process.stdin.flush()

    deadline = time.monotonic() + 10
    finished = None
    while time.monotonic() < deadline:
        message = messages.get(timeout=10)
        if message.get("type") == "frame":
            frames += 1
        if message.get("type") == "finished":
            finished = message
            break
    else:
        raise TimeoutError("playback did not finish")

    process.wait(timeout=5)
    assert process.returncode == 0
    assert finished is not None
    assert frames >= 20
    assert finished.get("processed_frames") == frames
    print(
        "playback-control-ok "
        f"frames={frames} "
        f"events={finished.get('recognized_events')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

