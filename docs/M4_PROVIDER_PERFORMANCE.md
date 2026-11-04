# M4 Provider 与性能后端记录

状态：CPU/DirectML Provider 接入完成

## 1. 背景

本机有 NVIDIA RTX 3060，但没有 CUDA Toolkit 和 cuDNN 开发/运行环境，因此当前无法直接验证 CUDA Execution Provider。

Windows DirectML 可以直接使用显卡，不需要 CUDA Toolkit，因此先接入 DirectML 作为 GPU 路径。

## 2. Provider 支持

ONNX Runtime 默认包已切换为：

```text
Microsoft.ML.OnnxRuntime.DirectML 1.20.1
```

支持：

- `cpu`
- `dml`
- 检测器与识别器独立 Provider
- DirectML 设备索引

CLI：

```powershell
plate_cli.exe --provider cpu
plate_cli.exe --provider dml --device-id 0
plate_cli.exe --detector-provider dml --recognizer-provider cpu
```

Bench：

```powershell
plate_bench.exe --provider dml --device-id 0
```

Qt 设置界面可以选择 CPU 或 DirectML，并配置设备索引。

## 3. 验证结果

### DirectML 可用性

合成 ONNX 识别模型在 DirectML 上成功初始化并推理。

```text
CPU p50：0.341 ms
DirectML p50：0.343 ms
```

该模型是固定输出的小型测试模型，延迟差异没有实际优化参考价值，只用于验证 Provider 链路。

### HyperLPR 兼容性

HyperLPR 当前检测模型和识别模型在 DirectML 下都会在 Session 初始化阶段失败，ONNX Runtime 返回空异常信息。

已确认：

- DirectML Provider 本身可用；
- CPU 下 HyperLPR 两个模型正常；
- 失败发生在 HyperLPR 模型初始化，而不是 DLL 加载或设备选择。

因此当前默认策略：

- HyperLPR3：CPU；
- 兼容 DirectML 的通用 ONNX 模型：可使用 DirectML；
- Qt 默认 CPU，用户可手动切换。

## 4. 当前真实 CPU 基线

HyperLPR 真实裁剪识别：

```text
样本数：4
完全匹配率：100%
字符准确率：100%
p50：11.915 ms
p95：12.763 ms
```

完整图片端到端：

```text
检测 + 四点矫正 + 识别
CPU 约 54-67 ms
```

## 5. 为什么暂不上 CUDA

CUDA Provider 需要：

- CUDA Toolkit；
- cuDNN；
- 对应 ONNX Runtime GPU 包；
- 匹配的显卡驱动。

当前环境只有显卡驱动，没有 CUDA/cuDNN。强行接入 CUDA 包只会让运行库加载失败，不能产生可信性能数据。

## 6. 下一阶段

要继续提升 HyperLPR GPU 性能，建议：

1. 导出 DirectML 兼容的检测/识别模型；
2. 或安装 CUDA 12.x、cuDNN 9.x 和 ONNX Runtime GPU；
3. 用相同测试集比较 CPU、DirectML 和 CUDA；
4. 固定 Provider、模型哈希、输入尺寸和阈值；
5. 扩大真实验证集到至少 200 张完整场景图片。

