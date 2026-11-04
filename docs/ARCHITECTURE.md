# 实时车牌识别与通行记录系统：整体架构设计

状态：Phase 0 评审稿

## 1. 项目目标

构建一个 Windows 桌面系统，完成以下能力：

- 读取图片、本地视频、RTSP 视频流和摄像头输入；
- 检测中国车牌，完成透视矫正，识别蓝牌和绿牌；
- 在连续帧中跟踪车牌，每次车辆通过只产生一条稳定识别事件；
- 在 Qt 桌面应用中显示实时画面和识别结果；
- 使用 SQLite 保存可检索的通行记录和截图；
- 记录各阶段耗时、吞吐量、准确率和失败统计；
- 保持推理、跟踪、存储和界面之间的边界，方便替换模型或扩展多路视频。

项目重点是“可部署的 C++ 实时流水线”，不是只停留在 Python Notebook 中的模型演示。

## 2. 首版非目标

- 云端平台或 Web 管理后台
- 在桌面应用内部实现完整模型训练
- 多租户、计费或远程设备管理
- 车辆品牌和车型识别
- 在没有定义测试集的情况下承诺全天候识别效果
- 直接控制停车场道闸或硬件继电器

架构需要支持后续扩展多路视频，但首版只验证单路视频的完整闭环。

## 3. 环境与约束

本机已确认的开发环境：

- Visual Studio 2022 Community
- MSVC 14.44
- Visual Studio 自带 CMake 与 Ninja
- Visual Studio 自带 vcpkg
- NVIDIA GeForce RTX 3060 Laptop GPU，6 GB 显存

仍需安装的依赖：

- Qt 6.8 LTS，MSVC 2022 64 位版本
- OpenCV 4.10 或更高版本
- 与所选 CUDA 版本匹配的 ONNX Runtime GPU 包
- 通过 vcpkg 安装 SQLite3、nlohmann/json、spdlog 和 Catch2

模型文件和测试视频不直接提交到 Git。项目通过模型清单记录模型版本、文件名和 SHA-256。

## 4. 关键架构决策

### ADR-001：产品运行时使用 C++17 和 Qt 6

模型训练和导出可以使用 Python，但最终应用使用 C++17。这样部署时不需要 Python 运行时，也能延续现有的 C++/Qt 工程经验。

### ADR-002：采用两阶段车牌流水线

先使用车牌检测模型，再执行透视矫正和字符识别。检测和识别可以独立评估、独立替换，不会被绑定为一个黑盒模型。

初始模型候选：

- 检测模型：基于 CCPD 微调的 YOLOv8n 或 YOLO11n，导出为 ONNX
- 矫正模块：四点透视变换或旋转框矫正
- 识别模型：基于 CTC 的 LPRNet，支持 7 位蓝牌和 8 位绿牌

检测适配器与具体模型解耦。后续替换检测模型时，不需要修改流水线的其他部分。

### ADR-003：先使用 ONNX Runtime，后续再考虑 TensorRT

ONNX Runtime 可以用同一份模型支持 CPU 和 CUDA Execution Provider，适合作为首版部署路径。TensorRT、FP16 等优化安排在端到端指标稳定之后。

### ADR-004：先完成无界面流水线

核心流水线必须先通过命令行程序跑通，再接入 Qt 界面。这样模型比较、回归测试和性能测试都不依赖界面行为。

### ADR-005：使用本地 SQLite，不引入后端服务

识别事件和摄像头信息保存在本地 SQLite 数据库中，开启 WAL 模式。首版不需要额外后端服务。

### ADR-006：先跟踪，再持久化

单帧识别结果不直接写入数据库。跟踪器和稳定判定策略确认车牌后，才生成通行记录，避免同一辆车在连续帧中重复入库。

### ADR-007：采用最新帧优先的背压策略

采集线程到推理线程之间使用有界队列。推理速度跟不上采集速度时，丢弃旧帧以保持实时性，同时统计并展示丢帧数量。

## 5. 系统上下文

```text
图片 / 视频 / RTSP / 摄像头
              |
              v
      +----------------+
      |     采集层      |
      +----------------+
              |
              v
      +----------------+
      |     帧队列      |  有界队列，丢弃最旧帧
      +----------------+
              |
              v
      +-------------------------------+
      |          识别流水线            |
      | 检测 -> 矫正 -> 识别 -> 跟踪   |
      | -> 稳定文本投票                |
      +-------------------------------+
          |                    |
          v                    v
  +----------------+   +----------------+
  |  Qt 界面事件    |   |    事件队列     |
  +----------------+   +----------------+
          |                    |
          v                    v
  +----------------+   +----------------+
  |   实时预览      |   | SQLite + 截图  |
  |   结果列表      |   |   记录检索      |
  +----------------+   +----------------+
```

## 6. 工程分层

```text
apps/plate_desktop       Qt 桌面应用、组合根、视图模型
apps/plate_cli           无界面运行器和性能测试入口

libs/core                领域类型、端口接口、流水线编排
libs/video               图片、视频、RTSP 和摄像头数据源
libs/inference           ONNX Runtime 检测、矫正和识别适配器
libs/tracking            IOU 或 ByteTrack 兼容跟踪器
libs/storage             SQLite 仓储和截图写入

tests/unit               纯逻辑和适配器单元测试
tests/integration        确定性的视频到记录集成测试
tools/model_export       Python 模型导出与校验工具
configs                  运行时配置和模型清单
models                   本地模型文件，不提交 Git
data                     本地数据库和截图，不提交 Git
```

依赖规则：

- `core` 不依赖 Qt、ONNX Runtime、SQLite 或任何界面模块。
- `inference`、`video`、`tracking` 和 `storage` 实现 `core` 中声明的端口接口。
- `plate_desktop` 和 `plate_cli` 是组合根，负责选择并装配具体实现。

## 7. 领域类型

```cpp
struct Frame {
    std::uint64_t stream_id;
    std::uint64_t sequence;
    std::int64_t source_timestamp_us;
    std::int64_t receive_timestamp_us;
    cv::Mat bgr;
};

struct PlateBox {
    cv::Rect2f bounds;
    std::array<cv::Point2f, 4> quad;
    float confidence;
};

struct PlateText {
    std::string text;
    PlateColor color;
    float confidence;
    std::vector<float> character_scores;
};

struct PlateObservation {
    std::uint64_t stream_id;
    std::uint64_t frame_sequence;
    std::int64_t timestamp_us;
    PlateBox box;
    std::string text;
    PlateColor color;
    float text_confidence;
};

struct PlateTrack {
    std::uint64_t track_id;
    std::string stable_text;
    PlateColor color;
    float confidence;
    int hit_count;
    int missed_count;
    std::int64_t first_seen_us;
    std::int64_t last_seen_us;
    bool confirmed;
};

struct RecognitionEvent {
    std::uint64_t event_id;
    std::uint64_t stream_id;
    std::uint64_t track_id;
    std::string plate_number;
    PlateColor color;
    float confidence;
    std::int64_t captured_at_us;
    std::string snapshot_path;
};
```

`Frame` 在阶段之间通过 `std::shared_ptr` 传递，后续也可以替换为帧缓冲池。`cv::Mat` 自带引用计数，界面预览不需要复制整块像素数据。

## 8. 核心端口接口

```cpp
class IVideoSource {
public:
    virtual ~IVideoSource() = default;
    virtual bool open(const SourceConfig& config, std::string& error) = 0;
    virtual bool read(Frame& frame, std::chrono::milliseconds timeout) = 0;
    virtual void close() = 0;
};

class IPlateDetector {
public:
    virtual ~IPlateDetector() = default;
    virtual std::vector<PlateBox> detect(
        const cv::Mat& frame,
        std::string& error) = 0;
};

class IPlateRectifier {
public:
    virtual ~IPlateRectifier() = default;
    virtual cv::Mat rectify(
        const cv::Mat& frame,
        const PlateBox& box) = 0;
};

class IPlateRecognizer {
public:
    virtual ~IPlateRecognizer() = default;
    virtual PlateText recognize(
        const cv::Mat& plate_image,
        std::string& error) = 0;
};

class IPlateTracker {
public:
    virtual ~IPlateTracker() = default;
    virtual PlateTrack update(
        const PlateObservation& observation) = 0;
    virtual void reset() = 0;
};

class IRecognitionRepository {
public:
    virtual ~IRecognitionRepository() = default;
    virtual bool save(
        const RecognitionEvent& event,
        std::string& error) = 0;
    virtual std::vector<RecognitionEvent> query(
        const RecordQuery& query) = 0;
};
```

各适配器通过错误通道上报运行失败。内部可以使用异常，但异常不能跨越线程或模块边界。

## 9. 运行时组件

### CaptureService

- 每路视频拥有一个 `IVideoSource`。
- 为采集帧分配递增序号和源时间戳。
- 将帧写入有界的最新帧队列。
- 统计采集帧率、读取失败次数和丢帧数量。

### RecognitionEngine

- 持有检测器、矫正器、识别器和跟踪器。
- 首版按单路视频顺序处理帧。
- 输出单帧观察结果、稳定识别事件和各阶段耗时。
- 支持停止信号，保证退出时能够取消阻塞任务。

### EventCoordinator

- 执行车牌格式校验、稳定文本投票和重复事件抑制。
- 将界面事件投递到 Qt 主线程。
- 将数据库写入投递到独立队列，不阻塞推理线程。

### RecordService

- 通过仓储接口保存确认后的识别事件。
- 原子写入截图：临时文件、刷新、重命名。
- 提供按时间、车牌和摄像头查询，以及 CSV 导出。

### Desktop Application

- Qt Widgets 只负责界面展示和用户操作。
- 不在绘制函数或按钮回调中直接执行模型推理或 SQLite 查询。
- 通过队列信号接收不可变的预览帧和识别事件。

## 10. 线程模型

```text
主线程 / UI 线程
    接收预览帧快照和识别事件

采集线程，每路视频一个
    只负责阻塞读取视频源

识别工作线程，每路活动视频一个
    检测 -> 矫正 -> 识别 -> 跟踪 -> 事件判定

存储写入线程，全进程一个
    串行执行 SQLite 写入和截图元数据保存

模型加载
    在流水线启动前完成，不占用 UI 线程
```

队列策略：

| 队列 | 容量 | 溢出策略 |
| --- | ---: | --- |
| 采集 -> 识别 | 3 帧 | 丢弃最旧帧 |
| 识别 -> UI 预览 | 1 帧 | 覆盖上一帧预览 |
| 稳定事件 -> 存储 | 128 条事件 | 施加背压并记录日志 |

界面不会接收所有处理帧。预览图经过采样和缩放，仅用于显示。

## 11. 识别与去重策略

1. 在当前帧中检测一个或多个车牌候选框。
2. 按置信度排序并执行非极大值抑制。
3. 将车牌区域矫正到统一尺寸。
4. 识别字符和车牌颜色。
5. 校验省份、字母、数字、总长度以及蓝牌/绿牌规则。
6. 根据 IOU 和中心点距离将观察结果关联到已有跟踪目标。
7. 同一目标累计至少 3 次命中后确认。
8. 对最近 5 次观察结果进行加权投票，生成稳定车牌号。
9. 只有目标已确认且车牌号稳定时才生成通行事件。
10. 同一摄像头、同一归一化车牌在冷却时间内只产生一条记录。

初始默认参数：

- IOU 关联阈值：0.30
- 跟踪目标确认次数：6 次
- 投票窗口：10 次观察
- 稳定文本占比：70%
- 最短有效跟踪时间：800 ms
- 同车牌记录冷却时间：30 秒
- 同一跟踪目标最多生成一次记录
- 跟踪目标过期时间：连续 2 秒无观察

这些参数全部通过配置读取，不写死在业务逻辑中。

## 12. 数据存储

SQLite schema 版本 1：

```sql
CREATE TABLE cameras (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    source_uri TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE recognition_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    camera_id INTEGER NOT NULL,
    track_id INTEGER NOT NULL,
    plate_number TEXT NOT NULL,
    plate_color TEXT NOT NULL,
    confidence REAL NOT NULL,
    captured_at_ms INTEGER NOT NULL,
    snapshot_path TEXT,
    bbox_json TEXT,
    created_at_ms INTEGER NOT NULL,
    FOREIGN KEY (camera_id) REFERENCES cameras(id)
);

CREATE INDEX idx_events_time
    ON recognition_events(captured_at_ms DESC);
CREATE INDEX idx_events_plate
    ON recognition_events(plate_number, captured_at_ms DESC);
CREATE INDEX idx_events_camera_time
    ON recognition_events(camera_id, captured_at_ms DESC);

CREATE TABLE schema_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

数据库设置：

- 开启 WAL 日志模式
- 开启外键约束
- 配置忙等待超时
- 通过 `schema_meta` 记录 schema 版本和迁移状态

截图目录：

```text
data/snapshots/YYYY/MM/DD/<camera>/<timestamp>_<plate>.jpg
```

截图默认只保存在本机。保留周期可配置。公开展示时使用测试视频或不包含真实车主信息的素材。

## 13. 配置

```json
{
  "models": {
    "detector_path": "models/plate_detector.onnx",
    "recognizer_path": "models/plate_recognizer.onnx",
    "provider": "cuda",
    "intra_threads": 2
  },
  "pipeline": {
    "queue_capacity": 3,
    "target_process_fps": 25,
    "stable_hits": 6,
    "vote_window": 10,
    "min_track_duration_ms": 800,
    "record_cooldown_ms": 30000,
    "track_ttl_ms": 2000
  },
  "storage": {
    "database_path": "data/plate_vision.db",
    "snapshot_directory": "data/snapshots",
    "retention_days": 30
  }
}
```

配置在启动时统一校验。模型尺寸不匹配、Provider 不支持或路径不可写时，必须在视频开始处理前给出明确错误。

## 14. 错误处理与可观测性

- `core` 返回带上下文的类型化错误码，不直接依赖日志库。
- 模型加载失败时终止启动，不静默降级到未知模型。
- 视频源读取失败时执行有上限的指数退避重试。
- 单路视频失败不能影响其他视频继续运行。
- 日志同时输出到控制台和本地滚动文件。
- 日志不能包含 RTSP 凭据或完整个人敏感信息。

必须采集的指标：

- 采集帧率和丢帧数量
- 检测、矫正、识别及端到端耗时
- 每帧候选车牌数量和已确认跟踪目标数量
- 数据库写入耗时和失败次数
- CPU、内存和 GPU 显存占用
- p50 和 p95 端到端延迟

## 15. 性能策略

第一阶段：

- 单路视频，batch size 为 1
- 检测输入尺寸为 640 或 960 像素
- 使用 ONNX Runtime CUDA Execution Provider
- 预览图缩小后再传回 Qt
- 使用有界队列和最新帧丢弃策略

后续优化：

- FP16 推理
- TensorRT Execution Provider
- 帧缓冲池
- FFmpeg 硬件解码
- 分阶段性能分析，必要时增加批量检测

写入简历的性能数据必须来自 benchmark 工具，并注明显卡、分辨率、模型和运行时配置。

## 16. 测试策略

单元测试：

- letterbox 和坐标反变换
- 非极大值抑制
- 透视矫正
- 车牌格式校验
- 跟踪目标关联、确认和过期
- 稳定文本投票和重复事件抑制
- 数据库迁移和查询

集成测试：

- 固定视频配合模拟检测器/识别器时结果可重复
- 固定视频配合真实模型时能够生成截图和 CSV 记录
- 队列溢出时丢帧且内存不会持续增长
- 重启后记录仍然存在，schema 版本正确

质量测试：

- 白天、夜间、逆光、运动模糊、倾斜和局部遮挡
- 蓝牌和绿牌
- 画面中单车和多车
- 超过冷却时间的重复通行

性能测试：

- 各阶段 p50/p95 耗时
- 720p 和 1080p 帧率
- 连续运行 60 分钟并周期采样内存和句柄数量

## 17. 里程碑

### M0：架构基线

- 确认本架构文档
- 安装并固定依赖版本
- 创建工程目录和 CMake Presets
- 完成 MSVC、Qt、OpenCV、CUDA 和 ONNX Runtime 冒烟测试

### M1：无界面流水线

- 图片和视频数据源
- 检测器与识别器适配器
- 透视矫正和车牌校验
- 带标注输出的 CLI
- 阶段耗时统计和确定性测试

验收标准：不依赖 Qt 的前提下，一段 1080p 测试视频可以输出标注视频和 JSON 识别结果。

### M2：Qt 桌面应用

- 实时预览
- 启动、停止和数据源选择
- 识别结果列表和截图预览
- 模型与推理 Provider 状态

验收标准：桌面应用处理视频时 UI 线程不阻塞。

### M3：跟踪与通行记录

- 跟踪关联和稳定文本投票
- 重复事件抑制
- SQLite 持久化与检索
- CSV 导出和记录保留策略

验收标准：一次车辆通过只产生一条记录，记录在重启后可查询。

### M4：质量与发布

- 性能基准表
- 长时间运行测试
- 错误恢复
- 安装包或便携版发布包
- 架构图、README 和 3 分钟演示视频

验收标准：发布包能够在满足依赖说明的干净 Windows x64 环境运行。

## 18. 首轮实现顺序

1. 创建 CMake 工程和依赖检查。
2. 实现领域类型、模拟检测器/识别器和视频循环。
3. 增加 CLI 运行器和确定性测试。
4. 接入真实 ONNX 检测模型。
5. 接入透视矫正和 LPRNet 识别。
6. 增加跟踪和稳定事件策略。
7. 接入 SQLite 持久化。
8. 接入 Qt 桌面应用。
9. 基线指标稳定后再做性能优化。

## 19. 风险与应对

| 风险 | 应对措施 |
| --- | --- |
| 模型成为关键路径 | 保持端口接口，使用模拟适配器并行推进流水线开发 |
| Qt 环境准备耗时 | 先完成无界面 CLI |
| 小目标或旋转车牌漏检 | 保留两级车辆 ROI 方案，并按分辨率测试 |
| 跟踪丢失后重复入库 | 使用车牌归一化、摄像头冷却时间和目标重识别策略 |
| GPU Provider 配置不稳定 | 保留 CPU 回退，并在所有基准中记录 Provider |
| 模型许可证不清晰 | 在模型清单和 README 中记录模型与数据集许可证 |
| 公开演示泄露隐私 | 使用经过授权或合成的测试素材，并明确数据保留策略 |

## 20. Phase 0 交付物

本文档确定模块边界、接口、线程模型、数据模型、质量门槛和实现顺序。下一阶段从 CMake 工程骨架、依赖冒烟检查、核心领域类型和使用模拟适配器的无界面视频循环开始。
