# Plate Vision Desktop (Qt 6 C++)

Native Qt Widgets desktop application.

## Build

Qt 6.12 MinGW 13.1:

```powershell
$env:PATH = "D:\DownLoad\Qt\6.12.0\mingw_64\bin;" +
  "D:\DownLoad\Qt\Tools\mingw1310_64\bin;" +
  "D:\DownLoad\Qt\Tools\Ninja;" + $env:PATH

cmake --preset qt-release
cmake --build --preset qt-release
```

Output:

```text
build/desktop-qt/plate_desktop.exe
```

`windeployqt` runs automatically after build and deploys Qt DLLs, platform plugins, image plugins, MinGW runtime, and SQLite driver.

## Run

```powershell
build\desktop-qt\plate_desktop.exe
```

The default backend is:

```text
build/release/bin/Release/plate_cli.exe
```

Override it with `PLATEVISION_CLI`.

## Features

- local video and RTSP input
- HyperLPR3, generic ONNX, and mock engines
- live preview and overlays
- 0.5x/1.0x/1.5x/2.0x playback speed
- click video to pause/resume
- real-time event table
- shared snapshot preview below live and historical records
- SQLite history
- historical snapshot viewing
- CSV export
- model and storage settings

## Architecture

```text
Qt Widgets UI
    -> QProcess
    -> plate_cli.exe --ui-stream --control-stdin
    -> JSON Lines preview and events
    -> SQLite and snapshots written by C++ backend
```

## Smoke Test

```powershell
plate_desktop.exe --smoke-test `
  --source output\hyperlpr_pass.mp4 `
  --engine hyperlpr `
  --screenshot output\desktop_cpp_smoke.png
```

