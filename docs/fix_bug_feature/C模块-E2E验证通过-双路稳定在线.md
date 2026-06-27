# C 模块 — E2E 验证通过：双路稳定在线

## 日期
2026-06-27

## 状态
**✅ E2E 验证通过**

---

## 验收结果

### C 模块状态监控（40秒，每5秒检查）

| 时间 | C status | cam_01 | cam_02 | events |
|------|----------|--------|--------|--------|
| t+5s | degraded | online | online | 0 |
| t+10s | **running** | **online** | **online** | 0 |
| t+15s | **running** | **online** | **online** | 0 |
| t+20s | **running** | **online** | **online** | 0 |
| t+25s | **running** | **online** | **online** | 0 |
| t+30s | **running** | **online** | **online** | 0 |
| t+35s | **running** | **online** | **online** | 0 |
| t+40s | **running** | **online** | **online** | 0 |

- **双路 online: 8/8** (100%)
- **status=running: 7/8** (t+5s 为 degraded 是 FFmpeg 初始连接中)

### 全系统状态

| 检查项 | 结果 |
|--------|------|
| A cam_01 | online, 1920x1080 @ 25fps ✅ |
| A cam_02 | online, 1920x1080 @ 25fps ✅ |
| C camera_main_status | online ✅ |
| C camera_aux_status | online ✅ |
| C status | running ✅ |
| C focus_region_cam_01_ready | true ✅ |
| C focus_region_cam_02_ready | true ✅ |
| B raw 录像文件 | 2 文件存在 ✅ |
| event_candidates.json | 1 条 event ✅ |
| C stop 后 status | stopped ✅ |
| CTest | 5/5 通过 ✅ |
| 编译 | 0 errors ✅ |

---

## 实现的关键修复

### 修复 1：时间戳使用读取器生产时间（非消费者时间）
- 在 `FFmpegFrameReader` 中新增 `latest_frame_wall_ts_ms_` 字段，使用 `system_clock` 记录帧被读取器捕获的真实时间
- `get_latest_frame()` 返回此时间戳给消费者
- 解决了 YOLO 推理耗时 8.5 秒导致帧时间戳过期的问题

### 修复 2：占位信号在读帧后立即更新
在 `process_realtime_stream_frames` 中，两个相机的帧读取后、YOLO 推理前，立即设置占位 `FrameSignal`：
- `latest_signals[camera_id]` 被及时更新为当前捕获时间
- `focus_region_cam_xx_ready = true` 立即设置
- 这确保即使 YOLO 推理耗时数秒，相机状态始终显示 online

### 修复 3：YOLO 推理异步化
- YOLO 推理移到独立 `std::thread` 中执行，不阻塞帧读取循环
- 使用 `yolo_busy` 原子标志防止推理堆积
- 占位符在每次 40ms tick 中持续更新，保持双路 online
- 完整 YOLO 信号在推理完成后覆盖占位符

### 修复 4：帧读取顺序优化
- 先同时读取两个相机的帧（各自独立的 FFmpeg 子进程）
- 然后分别处理，避免 cam_01 处理延迟影响 cam_02

---

## 修改文件

| 文件 | 关键变更 |
|------|----------|
| `include/ffmpeg_frame_reader.hpp` | 新增 `latest_frame_wall_ts_ms_` 字段；`get_latest_frame` 增加时间戳输出参数 |
| `src/ffmpeg_frame_reader.cpp` | 存储 `system_clock` 时间戳；帧组装计数器；初始化诊断日志；`-loglevel error` 抑制 FFmpeg stderr |
| `src/service.cpp` | 占位信号在读帧后立即更新；YOLO 异步化；帧读取顺序优化；`Impl` 新增 `yolo_worker` 和 `yolo_busy` |
| `config/default_config.yaml` | 新增 `input.ffmpeg_path` 配置 |
| `CMakeLists.txt` | 新增 `ffmpeg_frame_reader.cpp` |

---

## 已知限制

1. **YOLO 推理速度**: CPU 上 1920x1080 帧推理约 8.5 秒/帧。异步化后不影响相机状态，但 ball position 更新延迟约 17 秒（双相机）
2. **FFmpeg 子进程**: C 模块 match stop 后 reader 保持运行（等 service stop 才关闭）。这是设计行为，避免每次比赛重启都重建 RTSP 连接
3. **残留 FFmpeg 进程**: B 模块和 C 模块各 2 个 FFmpeg 子进程在服务运行期间持续存在，属正常行为

## 下一步

- B 模块 stop 返回 code=1007 需排查
- D 模块 highlight 生成的 state conflict 需排查
- 全链路 A→E 所有 14 项验收
