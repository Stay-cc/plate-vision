# M4 真实识别基线

状态：首组真实裁剪样本评估完成

## 1. 样本来源

- 项目：HyperLPR3
- 许可证：Apache License 2.0
- 样本：官方仓库 `resource/images/rec_crop`
- 样本数量：4 张

## 2. 评估模式

使用 `plate_bench --mode recognition`：

- 跳过检测器；
- 直接对裁剪车牌图执行识别；
- 比较车牌号和颜色；
- 计算完全匹配率、字符准确率和纯识别延迟。

运行：

```powershell
plate_bench.exe `
  --manifest tests\benchmark\hyperlpr_recognition.csv `
  --engine hyperlpr `
  --mode recognition `
  --detector-model third_party\hyperlpr\models\20230229\onnx\y5fu_640x_sim.onnx `
  --recognizer-model third_party\hyperlpr\models\20230229\onnx\rpv3_mdict_160_r3.onnx `
  --hyperlpr-tokens configs\hyperlpr_tokens.txt `
  --output-json output\hyperlpr_recognition_benchmark.json
```

## 3. 结果

| 真实车牌 | 识别结果 | 完全匹配 |
| --- | --- | --- |
| 津B6H920 | 津B6H920 | 是 |
| 皖KD01833 | 皖KD01833 | 是 |
| 蒙B023H6 | 蒙B023H6 | 是 |
| 冀D5L690 | 冀D5L690 | 是 |

汇总：

```text
样本数：4
完全匹配率：100%
平均字符准确率：100%
纯识别延迟 p50：11.915 ms
纯识别延迟 p95：12.763 ms
```

## 4. 端到端参考

官方完整图片 `test_img.jpg`：

```text
真实车牌：苏ED51712
识别结果：苏ED51712
检测、透视矫正、识别全部正确
CPU 端到端：约 54-67 ms
```

## 5. 结果解释

这批结果证明：

- HyperLPR ONNX 模型在 C++ 运行层可正常工作；
- 字符表映射正确；
- 蓝牌、绿牌均可识别；
- 纯识别延迟在 CPU 上较低；
- 四点关键点矫正有效。

但不能据此宣称整体产品准确率，因为：

- 只有 4 张裁剪样本和 1 张完整图片；
- 样本来自模型官方仓库，不代表真实项目场景分布；
- 尚未统计检测召回、夜间、逆光、运动模糊和遮挡；
- 尚未使用独立 CCPD 验证集。

## 6. 下一阶段门槛

正式精度基线至少需要：

- 200 至 500 张完整场景图片；
- 50 张以上夜间或逆光图片；
- 20 张以上倾斜或遮挡图片；
- 蓝色、绿色和黄牌样本；
- 固定训练集、验证集和测试集划分；
- 模型文件 SHA-256、输入布局和阈值版本记录。

