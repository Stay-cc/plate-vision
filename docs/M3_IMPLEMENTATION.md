# M3 跟踪去重与通行记录实现记录

状态：M3 核心闭环完成

关联文档：

- [整体架构设计](ARCHITECTURE.md)
- [模块划分与核心类设计](MODULE_AND_CLASS_DESIGN.md)
- [HyperLPR3 真实模型基线](M2_HYPERLPR_BASELINE.md)

## 1. 实现范围

M3 已完成不依赖 Qt 的记录闭环：

```text
单帧观察
    -> IOU 跟踪
    -> 稳定文本投票
    -> 重复事件过滤
    -> 通行事件
    -> 截图写入
    -> SQLite 持久化
    -> 记录查询与 CSV 导出
```

## 2. 核心组件

### StableTextVoter

- 最近 10 次观察滑动窗口；
- 按置信度加权统计车牌文本；
- 至少 6 次命中且加权占比达到 70% 才判定稳定；
- 跟踪目标持续至少 800 ms 后才允许生成记录；
- 同时统计蓝牌/绿牌投票；
- 自动清理超时跟踪目标。

### DuplicateEventFilter

- 过滤键为 `(camera_id, normalized_plate)`；
- 默认冷却时间 30 秒；
- 冷却时间可配置；
- 限制最大缓存条目，避免长期运行无限增长。
- 同一 `track_id` 最多产生一次事件，避免长时间跟踪超过冷却期后重复落库。

### EventCoordinator

- 只处理格式有效且跟踪目标已确认的观察；
- 稳定文本投票通过后才生成通行事件；
- 通行时间使用采集系统时间，不直接使用视频文件相对时间；
- 事件携带流 ID、摄像头 ID、跟踪 ID、车牌、颜色、置信度和检测框。

### SnapshotWriter

- 目录：

```text
data/snapshots/YYYY/MM/DD/<stream_id>/
```

- JPEG 编码；
- 先写临时文件，再原子重命名；
- 文件名包含微秒时间戳和 ASCII 车牌片段；
- 返回相对于截图根目录的路径。

### SqliteRecognitionRepository

- Windows 自带 `winsqlite3`；
- WAL 模式；
- 外键约束；
- `cameras`、`recognition_events`、`schema_meta` 表；
- 支持按摄像头、车牌、时间范围、分页查询；
- 进程重启后记录仍可读取。

### RecordService

- 独立存储线程；
- 有界阻塞队列，队列满时施加背压；
- 先保存截图，再写入数据库；
- 停止时处理完已入队记录；
- 写入错误通过回调输出，不阻塞识别线程。

### plate_records

新增记录查询工具：

```powershell
plate_records.exe --database data\plate_vision.db `
  --plate 京A12345 `
  --output-csv output\records.csv
```

支持：

- 按摄像头过滤；
- 按车牌过滤；
- 限制返回数量；
- CSV 导出。

## 3. 数据表

`recognition_events` 保存：

- 摄像头 ID；
- 流 ID；
- 跟踪 ID；
- 车牌号；
- 车牌颜色；
- 置信度；
- 捕获时间；
- 截图相对路径；
- 检测框 JSON；
- 入库时间。

## 4. 测试覆盖

新增并通过：

- 稳定文本投票；
- 冷却期内重复事件过滤；
- 三次观察后只产生一个事件；
- 截图写入；
- SQLite 写入；
- 重新打开数据库后查询；
- CSV 导出；
- 真实 HyperLPR 模型记录闭环。

Debug 和 Release 均通过 CTest。

## 5. 验收结果

### 模拟模型

输入：

```text
60 帧 1080p 合成视频
```

结果：

```text
处理帧：60
已确认观察：58
通行事件：1
截图：生成
SQLite：写入成功
```

### HyperLPR 真实模型

输入：

```text
5 帧真实车牌循环视频
真实车牌：苏ED51712
```

结果：

```text
已确认观察：3
通行事件：1
识别：苏ED51712
颜色：green
截图：生成
SQLite：写入成功
CSV：可查询导出
```

真实模型 CPU 端到端 p50 约 43 ms，p95 约 56 ms。

## 6. 当前限制

- 还没有 Qt 记录列表和截图预览；
- 数据库只配置了一个默认摄像头；
- 尚未实现记录保留周期清理；
- 尚未实现跨天/跨摄像头统计报表；
- 单目标跟踪仍为简单 IOU，不是 ByteTrack；
- 真实准确率仍需 CCPD 和自采集验证集。

## 7. 阶段判断

M3 的后端数据闭环已经完成。M2 仍未完全结束，因为 Qt 桌面界面尚未实现。

建议后续顺序：

1. 完成 Qt 实时预览、通行记录列表和截图查看；
2. 补真实验证集指标；
3. 再进入 CUDA/TensorRT 和长期稳定性优化。
