# M2 ONNX 推理运行层实现记录

状态：M2 第一阶段完成并通过本地测试

关联文档：

- [整体架构设计](ARCHITECTURE.md)
- [模块划分与核心类设计](MODULE_AND_CLASS_DESIGN.md)
- [M1 实现记录](M1_IMPLEMENTATION.md)

## 1. 本阶段目标

将 M1 的模拟检测器和识别器替换为可加载 ONNX 模型的运行层，同时保持：

- 核心流水线不依赖具体模型；
- 检测、矫正、识别和跟踪边界不变；
- CLI 仍可在模拟引擎和 ONNX 引擎之间切换；
- 模型输入输出布局通过配置选择；
- 没有业务权重时也能用合成模型做确定性验证。

## 2. 已实现内容

### ONNX Runtime 基础层

- 使用 ONNX Runtime 1.20.1 CPU 包；
- 单个环境对象复用；
- 模型文件、输入类型、输入尺寸和输入输出名称校验；
- CPU intra-op 线程数配置；
- ONNX Runtime 异常转换为模块错误字符串；
- Windows 下使用宽字符模型路径。

### YOLO 车牌检测适配器

已实现：

- BGR 转 RGB；
- letterbox 等比缩放和填充；
- HWC 转 NCHW、归一化到 `[0, 1]`；
- 支持 `[1, C, N]` 和 `[1, N, C]` 两种输出布局；
- 支持不同类别数量的检测输出；
- 置信度过滤；
- 类别无关 NMS；
- 从 letterbox 坐标反变换回原图；
- 支持归一化或像素坐标输出；
- 最大检测数量限制。

### LPRNet 识别适配器

已实现：

- 车牌区域缩放和 NCHW 预处理；
- 支持 `[1, T, C]` 和 `[1, C, T]` 两种输出布局；
- Softmax；
- CTC 贪心解码、空白符过滤和连续重复字符合并；
- 字符平均置信度；
- 根据 7 位/8 位结果推断蓝牌或绿牌；
- 符号表文件读取和默认中国车牌字符集。

### CLI 模型模式

新增参数：

- `--engine mock|onnx`
- `--detector-model`
- `--recognizer-model`
- `--alphabet`
- `--detector-layout`
- `--recognizer-layout`
- `--coords-normalized`
- `--score-threshold`
- `--iou-threshold`
- `--detector-input-width/height`
- `--recognizer-input-width/height`
- `--intra-threads`

JSON 报告新增 `engine` 字段。

## 3. 合成测试模型

测试模型生成脚本：

```text
tools/model_test/generate_test_models.py
```

生成的模型：

| 模型 | 用途 |
| --- | --- |
| `detector_constant.onnx` | 验证 `[1, C, N]` 检测输出 |
| `detector_detections_first.onnx` | 验证 `[1, N, C]` 检测输出 |
| `recognizer_constant.onnx` | 验证 `[1, T, C]` CTC 输出 |
| `recognizer_class_major.onnx` | 验证 `[1, C, T]` CTC 输出 |

这些模型输出固定张量，只用于验证：

- ONNX Runtime 加载与运行；
- 输入输出名称和形状解析；
- 预处理与坐标反变换；
- NMS；
- CTC 解码；
- 流水线与跟踪集成。

它们不具备真实车牌检测或识别精度。

## 4. 本地验收结果

构建环境：

- Windows x64
- Visual Studio 2022 / MSVC 19.44
- OpenCV 4.10
- ONNX Runtime 1.20.1 CPU

测试结果：

```text
Debug:   100% tests passed, 0 tests failed out of 1
Release: 100% tests passed, 0 tests failed out of 1
```

使用 ONNX 引擎处理 1080p 合成视频：

```text
输入帧数：60
处理帧数：60
丢帧数量：0
有效观察：60
已确认观察：58
Release 总耗时 p50：2.636 ms
Release 总耗时 p95：3.336 ms
识别文本：京A12345
车牌颜色：blue
```

以上耗时基于固定输出的合成 ONNX 模型，只代表运行层开销，不代表真实 YOLO/LPRNet 模型性能。

## 5. 当前限制

- 当前只部署 CPU Execution Provider；
- 尚未接入真实训练权重；
- 尚未用真实数据评估检测 mAP、字符准确率和端到端匹配率；
- 尚未实现 CUDA/cuDNN Provider 和 FP16；
- 尚未实现 Qt 界面；
- 尚未实现模型清单校验、模型版本切换和精度回归基线。

## 6. 下一阶段

继续 M2 的模型落地部分：

1. 获取或训练真实 YOLO 车牌检测模型；
2. 训练或导入 LPRNet 字符识别模型；
3. 建立 CCPD 验证集和自采集测试集；
4. 输出检测、识别和端到端指标；
5. 接入 CUDA Execution Provider；
6. 完成 Qt 实时预览与结果列表。
