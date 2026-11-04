# M2 Qt 桌面端实现记录

状态：已完成

## 1. 技术栈

- Qt 6.12
- Qt Widgets
- Qt SQL / SQLite driver
- MinGW 13.1
- C++17
- CMake + Ninja

桌面端是纯 C++ Qt 应用。识别后端继续由 MSVC 构建的 `plate_cli.exe` 提供，两者通过进程边界解耦，不涉及 ABI 混用。

## 2. 架构

```text
Qt Widgets UI process
    -> QProcess
    -> plate_cli.exe --ui-stream --control-stdin
    -> JSON Lines
    -> frame JPEG / events / status
```

Qt 进程只负责：

- 用户操作；
- 视频预览；
- 事件表格；
- SQLite 历史查询；
- 截图显示；
- CSV 导出。

C++ 后端负责：

- 视频采集；
- ONNX/HyperLPR 推理；
- 跟踪和稳定投票；
- 重复事件过滤；
- 截图写入；
- 通行记录持久化。

UI 线程不执行模型推理或视频阻塞读取。

## 3. 功能

- 本地视频打开；
- RTSP/文件地址输入；
- HyperLPR3、通用 ONNX、Mock 引擎切换；
- 实时预览和车牌框叠加；
- 0.5x、1.0x、1.5x、2.0x 播放倍速；
- 点击视频画面暂停/继续；
- 实时通行事件表；
- 实时与历史记录共用下方截图预览；
- 点击任意记录显示对应车牌截图；
- SQLite 历史记录；
- 历史截图查看；
- CSV 导出；
- 模型、字符表、数据库和截图目录设置；
- CPU/DirectML Provider 设置；
- 优雅停止。

## 4. 构建与部署

```powershell
$env:PATH = "D:\DownLoad\Qt\6.12.0\mingw_64\bin;" +
  "D:\DownLoad\Qt\Tools\mingw1310_64\bin;" +
  "D:\DownLoad\Qt\Tools\Ninja;" + $env:PATH

cd apps\plate_desktop_cpp
cmake --preset qt-release
cmake --build --preset qt-release
```

构建后 `windeployqt` 自动部署：

- Qt Core/Gui/Widgets/Sql；
- `qwindows.dll`；
- SQLite SQL driver；
- image format plugins；
- MinGW runtime DLL；
- D3D/OpenGL fallback DLL。

输出：

```text
build/desktop-qt/plate_desktop.exe
```

## 5. 停止协议

1. UI 向子进程标准输入写入 `stop`；
2. 后端停止采集；
3. 处理完记录队列；
4. 写入 SQLite 和截图；
5. 后端正常退出；
6. 超过 2 秒才执行强制终止。

## 5.1 播放控制

Qt 通过子进程标准输入发送：

```text
pause
resume
speed 0.5
speed 1.0
speed 1.5
speed 2.0
stop
```

后端 `PlaybackController` 控制采集读取节奏：

- 暂停时停止读取视频源；
- 恢复后从当前帧继续；
- 倍速调整下一帧的播放间隔；
- 不依赖 UI 定时器丢帧模拟暂停。

烟雾测试：

```text
playback-control-ok frames=5
```

## 6. 验收

使用真实 HyperLPR3 模型和 5 帧 `苏ED51712` 视频完成：

- 实时预览；
- 车牌框叠加；
- 实时事件写入；
- 截图预览；
- SQLite 历史记录；
- 历史页加载；
- 中文界面；
- 独立运行，不依赖开发环境 PATH。

截图：

- `output/desktop_cpp_smoke.png`
- `output/desktop_cpp_smoke_history.png`

## 7. 后端验收

Debug 和 Release CTest：

```text
100% tests passed, 0 tests failed
```

Qt 构建：

```text
plate_desktop.exe 构建成功
windeployqt 部署成功
Windows GUI 烟雾测试通过
```

