# M2 模型评估与回归工具说明

状态：已实现并通过合成模型验收

关联文档：

- [M2 ONNX 推理实现记录](M2_ONNX_IMPLEMENTATION.md)
- [整体架构设计](ARCHITECTURE.md)

## 1. 目标

在真实 YOLO/LPRNet 权重和验证集准备好之后，可以不做代码修改，直接比较不同模型的：

- 车牌检测召回；
- 检测框 IoU；
- 端到端车牌完全匹配率；
- 字符准确率；
- 单张图片端到端延迟；
- p50 和 p95 延迟。

本工具为后续模型选型、阈值调整和回归测试提供统一入口。

## 2. 评估清单格式

CSV 表头：

```csv
image,plate,color,x,y,width,height
```

字段说明：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `image` | 是 | 图片路径，相对路径以 CSV 所在目录为基准 |
| `plate` | 是 | 真实车牌号，支持中文 |
| `color` | 否 | `blue`、`green` 或留空 |
| `x/y/width/height` | 否 | 真实检测框，填写时必须四项齐全 |

示例：

```csv
image,plate,color,x,y,width,height
images/sample_001.jpg,京A12345,blue,660,450,600,180
images/sample_002.jpg,粤B12345D,green,,,,
```

没有检测框时仍可计算完全匹配率和字符准确率，但不会计算检测召回和 IoU。

## 3. 运行方式

```powershell
plate_bench.exe --manifest samples.csv `
  --engine onnx `
  --detector-model models\plate_detector.onnx `
  --recognizer-model models\plate_recognizer.onnx `
  --alphabet configs\plate_alphabet.txt `
  --output-json output\benchmark.json
```

纯车牌裁剪识别模式：

```powershell
plate_bench.exe --manifest crop_samples.csv `
  --engine hyperlpr `
  --mode recognition `
  --detector-model models\plate_detector.onnx `
  --recognizer-model models\plate_recognizer.onnx `
  --hyperlpr-tokens configs\hyperlpr_tokens.txt
```

`recognition` 模式跳过检测和透视矫正，直接对裁剪车牌图执行字符识别。检测模型参数仍由 CLI 接口校验，但在该模式下不参与计算。

评估 HyperLPR3 模型时使用：

```powershell
plate_bench.exe --manifest samples.csv `
  --engine hyperlpr `
  --detector-model third_party\hyperlpr\models\20230229\onnx\y5fu_640x_sim.onnx `
  --recognizer-model third_party\hyperlpr\models\20230229\onnx\rpv3_mdict_160_r3.onnx `
  --hyperlpr-tokens configs\hyperlpr_tokens.txt
```

模型格式选项：

- `--detector-layout channels-first|detections-first`
- `--recognizer-layout time-major|class-major`
- `--coords-normalized`
- `--score-threshold`
- `--iou-threshold`
- `--match-iou-threshold`
- `--detector-input-width/height`
- `--recognizer-input-width/height`
- `--intra-threads`

## 4. 指标定义

### 检测召回

只统计提供了真实检测框的样本。检测结果与真值框 IoU 大于等于 `match-iou-threshold` 时计为命中。

默认阈值：

```text
0.50
```

### 完全匹配率

预测车牌归一化后与真实车牌完全一致，并且颜色一致时计为匹配。真实颜色为空时不比较颜色。

### 字符准确率

基于 UTF-8 字符序列计算归一化编辑距离：

```text
1 - 编辑距离 / max(真实长度, 预测长度)
```

中文省份字符按一个字符计算。

### 延迟

单张图片从读取到识别完成的总耗时。包含：

- 图片读取；
- 检测预处理和推理；
- NMS 和坐标反变换；
- 透视矫正；
- 识别预处理和推理；
- CTC 解码。

## 5. 输出

默认输出到清单文件旁的 `*.benchmark.json`。

摘要字段：

- `samples`
- `loaded`
- `detection_found`
- `box_samples`
- `detection_recall`
- `valid_predictions`
- `exact_matches`
- `exact_match_rate`
- `mean_character_accuracy`
- `p50_latency_ms`
- `p95_latency_ms`

逐样本字段包含真实值、预测值、颜色、置信度、有效性、完全匹配、字符准确率、检测框 IoU、延迟和错误信息。

## 6. 合成模型验收

当前使用固定输出 ONNX 测试模型验证工具链路：

```text
samples=1
detection_found=1
detection_recall=1
exact_match_rate=1
mean_character_accuracy=1
detection_iou=1
```

该结果只验证：

- CSV 清单读取；
- 图片路径解析；
- ONNX 推理；
- IoU 和字符指标计算；
- JSON 输出。

它不代表真实车牌模型准确率。

## 7. 真实模型评估下一步

1. 准备 CCPD 验证集和自采集测试集。
2. 生成包含图片路径、真实车牌和检测框的 CSV。
3. 分别评估检测模型和识别模型。
4. 固定模型版本、阈值和输入布局。
5. 将 `exact_match_rate`、字符准确率和 p95 延迟作为回归门槛。
6. 在真实指标稳定后再接 CUDA Provider 和 Qt 界面。
