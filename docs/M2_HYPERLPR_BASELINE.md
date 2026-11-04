# HyperLPR3 真实模型基线

状态：真实模型接入完成，单图基线通过

## 1. 模型来源

- 项目：HyperLPR3
- 许可证：Apache License 2.0
- 模型版本：`20230229`
- 检测模型：`y5fu_640x_sim.onnx`
- 识别模型：`rpv3_mdict_160_r3.onnx`

模型和官方示例图片不提交到 Git，下载脚本：

```powershell
powershell -ExecutionPolicy Bypass -File tools\fetch_hyperlpr_assets.ps1
```

版权和来源说明见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。

## 2. 专用适配器

HyperLPR3 与通用 ONNX 模型格式不同，因此新增专用适配器。

### 检测模型

- 输入：`[1,3,640,640]` 或 `[1,3,320,320]`
- 输出：`[1,25200,15]` 或 `[1,6300,15]`
- 输出字段：
  - `0:4`：输入图像像素坐标下的 `cx/cy/w/h`
  - `4`：目标置信度
  - `5:13`：四个车牌关键点
  - `13:15`：两类车牌分数
- 后处理：
  - 置信度过滤；
  - 类别分数乘目标置信度；
  - 使用四点关键点进行透视矫正；
  - NMS；
  - 从 letterbox 坐标恢复到原图坐标。

### 识别模型

- 输入：`[1,3,48,160]`
- 输出：`[1,20,78]`
- 预处理：
  - BGR；
  - 按宽高比缩放；
  - 右侧零填充；
  - 归一化到 `[-1,1]`。
- 后处理：
  - Softmax；
  - 空白符过滤；
  - 连续重复字符合并；
  - CTC 贪心解码。

HyperLPR3 官方 token 表包含 77 个 token，但模型输出 78 类。项目在末尾增加 `<unknown>`，保证类别数量和索引安全。使用 HyperLPR 引擎时字符表路径为：

```text
configs/hyperlpr_tokens.txt
```

## 3. 命令行运行

```powershell
plate_cli.exe `
  --input third_party\hyperlpr\images\test_img.jpg `
  --output-video output\hyperlpr_annotated.mp4 `
  --output-json output\hyperlpr_results.json `
  --engine hyperlpr `
  --detector-model third_party\hyperlpr\models\20230229\onnx\y5fu_640x_sim.onnx `
  --recognizer-model third_party\hyperlpr\models\20230229\onnx\rpv3_mdict_160_r3.onnx `
  --hyperlpr-tokens configs\hyperlpr_tokens.txt `
  --score-threshold 0.30 `
  --iou-threshold 0.60
```

## 4. 单图基线

官方示例图片：

```text
真实车牌：苏ED51712
车牌颜色：绿色
```

Release CPU 结果：

```text
检测结果：1 个车牌
识别结果：苏ED51712
颜色：green
完全匹配：true
字符准确率：1.0
端到端耗时：约 54-67 ms
```

![HyperLPR 识别结果](../output/hyperlpr_preview.png)

评估报告：

```text
output/hyperlpr_benchmark_report.json
```

## 5. 测试覆盖

新增真实模型 CTest：

- 自动检查模型文件和示例图片是否存在；
- 不存在时跳过；
- 存在时执行检测、透视矫正、CTC 识别；
- 校验车牌完全匹配和绿色车牌推断。

## 6. 当前限制

- 目前只验证了官方单张示例图片，不能据此宣称整体准确率；
- 尚未使用 CCPD 验证集或自采集序列统计真实召回率；
- 当前为 CPU Execution Provider；
- 尚未接入 CUDA/cuDNN；
- 尚未使用 HyperLPR 分类模型区分黄色、双层等特殊车牌；
- 当前 C++ 适配器使用 Apache-2.0 模型与后处理行为，发布时需要保留第三方声明。

## 7. 下一步指标门槛

需要准备至少 200 至 500 张真实图片，覆盖白天、夜间、逆光、倾斜和遮挡场景，并使用 `plate_bench` 输出：

- 检测召回率；
- 完全匹配率；
- 平均字符准确率；
- p50/p95 延迟；
- 失败样本分类。

真实指标稳定后，再进入 Qt 界面和 CUDA Provider 优化。
