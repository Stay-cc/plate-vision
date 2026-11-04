# Plate Vision Desktop (Legacy PySide6 Fallback)

Legacy fallback desktop shell. The primary desktop implementation is now `apps/plate_desktop_cpp` using Qt 6 C++.

## Architecture

```text
PySide6 UI process
    -> QProcess
    -> C++ plate_cli --ui-stream
    -> JSON Lines preview frames and recognition events
    -> SQLite and snapshots written by the C++ backend
```

The UI process does not run inference or block on video decoding. The C++ backend owns capture, inference, tracking, deduplication, snapshots, and SQLite.

## Features

- Open local video or enter a file/RTSP source
- HyperLPR3, generic ONNX, and mock engines
- Live preview with plate overlays
- Live event table and snapshot preview
- Persistent history from SQLite
- Snapshot viewing from history
- CSV export
- Model and storage settings

## Run

```powershell
python apps\plate_desktop\app.py
```

Requirements:

```text
PySide6>=6.8
```

The default executable is:

```text
build/release/bin/Release/plate_cli.exe
```

Override it with:

```powershell
$env:PLATEVISION_CLI = "D:\path\to\plate_cli.exe"
```

## Smoke Test

```powershell
python apps\plate_desktop\app.py `
  --smoke-test `
  --source output\hyperlpr_pass.mp4 `
  --engine hyperlpr `
  --screenshot output\desktop_smoke.png
```

## Note

The local machine did not have a C++ Qt SDK available for CMake linking, so the desktop shell uses PySide6 while all recognition and storage logic remains in C++. The UI contract is JSON Lines and can be ported to Qt C++ without changing the backend.

