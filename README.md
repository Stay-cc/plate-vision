# Plate Vision

实时车牌识别与通行记录系统。

项目按阶段开发：

1. 整体架构与环境基线
2. 无界面推理流水线
3. Qt 桌面应用
4. 跟踪去重与持久化记录
5. 性能优化、稳定性测试与发布打包

设计文档：

- [整体架构设计](docs/ARCHITECTURE.md)
- [模块划分与核心类设计](docs/MODULE_AND_CLASS_DESIGN.md)
- [M1 实现与验收记录](docs/M1_IMPLEMENTATION.md)
- [M2 ONNX 推理实现记录](docs/M2_ONNX_IMPLEMENTATION.md)
- [M2 模型评估工具说明](docs/M2_EVALUATION.md)
- [HyperLPR3 真实模型基线](docs/M2_HYPERLPR_BASELINE.md)
- [M2 Qt 桌面端实现](docs/M2_DESKTOP_IMPLEMENTATION.md)
- [M3 跟踪去重与通行记录实现](docs/M3_IMPLEMENTATION.md)
- [M4 真实识别基线](docs/M4_REAL_RECOGNITION_BASELINE.md)
- [M4 Provider 与性能后端](docs/M4_PROVIDER_PERFORMANCE.md)

## 当前进度

M1 无界面流水线和 M2 ONNX 推理运行层已经实现并通过本地测试，包含：

- 领域类型与端口接口；
- 有界最新帧队列；
- 采集线程和识别工作线程；
- 模拟车牌检测器与识别器；
- 透视矫正、车牌格式校验和 IOU 跟踪；
- OpenCV 图片、视频和 RTSP 数据源；
- 带标注视频和 JSON 报告输出；
- CLI 与确定性测试。
- ONNX Runtime 检测器与识别器适配；
- YOLO 两种输出布局、NMS 和坐标反变换；
- LPRNet 两种输出布局与 CTC 解码；
- 可配置字符表、阈值和动态模型输入尺寸。
- HyperLPR3 真实检测与识别模型适配。
- 稳定文本投票和重复通行记录过滤。
- 截图保存、SQLite 持久化、记录查询和 CSV 导出。
- Qt 6 C++ 桌面端、实时预览、事件列表和历史截图。

仓库中的 ONNX 模型是用于接口测试的合成模型，真实 YOLO/LPRNet 权重仍需通过训练或外部导入。

## 目标环境

- Windows 10/11 x64
- Visual Studio 2022，包含 MSVC x64 工具链
- CMake 3.24 或更高版本
- Qt 6.8 LTS
- OpenCV 4.10 或更高版本
- ONNX Runtime，支持 CPU 与 CUDA Execution Provider
- NVIDIA GeForce RTX 3060 Laptop GPU，6 GB 显存

## 本地 OpenCV

M1 使用官方 OpenCV 4.10 Windows 预编译包，放在：

```text
third_party/opencv
```

ONNX Runtime 1.20.1 位于：

```text
third_party/onnxruntime
```

`third_party` 已加入 `.gitignore`，不提交二进制依赖。

## 构建

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

如果 CMake 不在 PATH 中，可以使用 Visual Studio 自带的 CMake。

## 运行

生成 1080p 模拟测试视频：

```powershell
plate_cli.exe --generate-sample output\m1_sample.mp4 `
  --frames 60 --width 1920 --height 1080 --fps 25
```

执行无界面识别流程：

```powershell
plate_cli.exe --input output\m1_sample.mp4 `
  --output-video output\m1_annotated.mp4 `
  --output-json output\m1_results.json `
  --plate 京A12345 --color blue --queue-capacity 128
```

输出：

- `*.mp4`：带检测框、车牌文本和帧号的标注视频；
- `*.json`：逐帧识别结果、跟踪 ID、有效性、确认状态和耗时统计。

实时模式下建议使用较小的队列容量，例如 8。容量不足时程序会按设计丢弃旧帧；离线完整处理测试视频时可增大队列容量。

## ONNX 推理

使用合成 ONNX 模型验证真实 ONNX Runtime 链路：

```powershell
plate_cli.exe --input output\m1_sample.mp4 `
  --output-video output\m2_onnx_annotated.mp4 `
  --output-json output\m2_onnx_results.json `
  --engine onnx `
  --detector-model tests\models\detector_constant.onnx `
  --recognizer-model tests\models\recognizer_constant.onnx `
  --alphabet configs\plate_alphabet.txt `
  --queue-capacity 128
```

检测模型支持以下输出布局：

- `--detector-layout channels-first`，例如 `[1, 5, N]`
- `--detector-layout detections-first`，例如 `[1, N, 5]`

识别模型支持以下输出布局：

- `--recognizer-layout time-major`，例如 `[1, T, C]`
- `--recognizer-layout class-major`，例如 `[1, C, T]`

动态输入尺寸可通过 `--detector-input-width`、`--detector-input-height`、`--recognizer-input-width` 和 `--recognizer-input-height` 配置。

执行 Provider：

```text
--provider cpu|dml
--detector-provider cpu|dml
--recognizer-provider cpu|dml
--device-id <n>
```

HyperLPR 当前权重只兼容 CPU；DirectML 用于兼容的通用 ONNX 模型。

## 模型评估

评估清单格式：

```csv
image,plate,color,x,y,width,height
images/sample_001.jpg,京A12345,blue,660,450,600,180
images/sample_002.jpg,粤B12345D,green,,,,
```

运行评估：

```powershell
plate_bench.exe --manifest configs\benchmark.example.csv `
  --detector-model models\plate_detector.onnx `
  --recognizer-model models\plate_recognizer.onnx `
  --alphabet configs\plate_alphabet.txt `
  --output-json output\benchmark.json
```

输出包括检测召回、端到端完全匹配率、平均字符准确率、p50/p95 延迟和逐样本结果。

## HyperLPR 真实模型

下载官方模型和示例图片：

```powershell
powershell -ExecutionPolicy Bypass -File tools\fetch_hyperlpr_assets.ps1
```

运行：

```powershell
plate_cli.exe `
  --input third_party\hyperlpr\images\test_img.jpg `
  --output-video output\hyperlpr_annotated.mp4 `
  --output-json output\hyperlpr_results.json `
  --engine hyperlpr `
  --detector-model third_party\hyperlpr\models\20230229\onnx\y5fu_640x_sim.onnx `
  --recognizer-model third_party\hyperlpr\models\20230229\onnx\rpv3_mdict_160_r3.onnx `
  --hyperlpr-tokens configs\hyperlpr_tokens.txt
```

官方示例图片的本地基线结果为 `苏ED51712`，绿色车牌，CPU 单张约 54-67 ms。该结果只代表单张示例，不代表完整数据集准确率。

## 通行记录

M3 默认写入 SQLite：

```text
data/plate_vision.db
```

截图目录：

```text
data/snapshots/
```

查询记录：

```powershell
plate_records.exe --database data\plate_vision.db `
  --plate 苏ED51712 `
  --output-csv output\records.csv
```

运行时可配置：

```text
--database <path>
--snapshot-dir <path>
--stable-hits <n>
--vote-window <n>
--record-cooldown-ms <n>
--no-records
```

## 桌面端

```powershell
$env:PATH = "D:\DownLoad\Qt\6.12.0\mingw_64\bin;" +
  "D:\DownLoad\Qt\Tools\mingw1310_64\bin;" +
  "D:\DownLoad\Qt\Tools\Ninja;" + $env:PATH

cd apps\plate_desktop_cpp
cmake --preset qt-release
cmake --build --preset qt-release

..\..\build\desktop-qt\plate_desktop.exe
```

Qt 6.12 C++ 桌面端通过 `QProcess` 启动 `plate_cli --ui-stream`，实时消费 JSON Lines 预览和识别事件。识别、跟踪、SQLite 和截图仍由 MSVC C++ 后端负责。

Qt 桌面端使用 MinGW，识别后端使用 MSVC；两者通过进程和 JSON Lines 协议通信，避免 ABI 混用。

桌面端支持：

- 0.5x、1.0x、1.5x、2.0x 播放倍速；
- 点击视频画面暂停/继续；
- 点击实时或历史记录，在右侧下方查看对应车牌截图。

