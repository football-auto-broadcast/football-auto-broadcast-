# C 模块 RTSP 输入层切换 FFmpeg 子进程方案

## 变更日期
2026-06-27

## 问题描述
视觉分析模块（C 模块，vision-event-module）使用 OpenCV `cv::VideoCapture` 同步阻塞式读取 RTSP 流时存在严重不稳定问题：
- **cam_02（辅机位 aux）** 长时间无法成功打开 RTSP 流，频繁 fallback 到模拟帧
- OpenCV 内置 FFmpeg 后端对 RTSP 超时/重连控制能力有限，`OPENCV_FFMPEG_CAPTURE_OPTIONS=rtsp_transport;tcp` 环境变量方案无法稳定解决问题
- 单路流失败会阻塞整个帧读取循环，影响整体服务稳定性
- ffprobe 和 A/B 模块均能稳定读取同一 RTSP 流，确认问题集中在 C 模块的 OpenCV RTSP 输入层

## 根本原因
1. OpenCV `cv::VideoCapture` 对 RTSP 流的超时和重连行为不可控，与底层 FFmpeg 交互存在黑盒行为
2. 同步 `capture.read()` 在流异常时无限阻塞或返回空帧，无有效恢复机制
3. cam_02 的 RTSP 流特征（GigE 辅机位，直接通过 MediaMTX 8555/aux 发布）触发 OpenCV FFmpeg 后端的特定兼容性问题

## 解决方案

### 总体设计
实现 `FFmpegFrameReader` 类，每路 RTSP 使用独立 FFmpeg 子进程 + 独立拉流线程，通过 Windows 匿名管道读取原始 BGR24 帧。

```
┌──────────────────────────────────────────────────────┐
│  VisionService (Data Worker Thread @ 25fps)         │
│                                                      │
│  process_realtime_stream_frames()                    │
│    │                                                 │
│    ├─► read_stream_frame("cam_01")                  │
│    │     └─► FFmpegFrameReader("main")              │
│    │           ├─ reader_thread_ ─► FFmpeg subprocess│
│    │           │   (独立线程)        │               │
│    │           │                    ├─ RTSP → BGR24  │
│    │           │                    └─ stdout pipe   │
│    │           └─ latest_frame_buffer_ (mutex)       │
│    │                                                 │
│    └─► read_stream_frame("cam_02")                  │
│          └─► FFmpegFrameReader("aux")               │
│                └─ (同上，独立线程+子进程)            │
└──────────────────────────────────────────────────────┘
```

### FFmpeg 命令行
```
ffmpeg -rtsp_transport tcp -fflags nobuffer -flags low_delay
       -i <rtsp_uri> -an -sn -dn -pix_fmt bgr24 -f rawvideo pipe:1
```

- `-rtsp_transport tcp`: 强制 TCP 传输，避免 UDP 丢包
- `-fflags nobuffer -flags low_delay`: 低延迟模式
- `-pix_fmt bgr24`: 输出 BGR24 格式，与 OpenCV `CV_8UC3` 直接兼容
- `-f rawvideo pipe:1`: 原始视频帧写入 stdout

### 帧读取线程
```
reader_thread_func():
  loop:
    启动 FFmpeg 子进程（CreateProcess + 匿名管道）
    ffmpeg_alive = true
    loop:
      ReadFile(pipe) → 累加到 assembly_buffer
      满一帧(width×height×3 bytes) → 写入 latest_frame_buffer (mutex)
      FFmpeg 退出/管道断裂 → break
    terminate_ffmpeg()
    sleep(reconnect_delay) → 重连
```

### 关键设计决策
| 决策 | 说明 |
|------|------|
| 独立子进程而非进程内 FFmpeg API | 隔离崩溃域，FFmpeg 子进程崩溃不影响服务主进程 |
| 每路独立线程 | cam_01/cam_02 互不阻塞，单路失败不影响另一路 |
| latest-frame 单帧缓冲 | 宁可丢帧不积压延迟，消费者 25fps 轮询取最新帧 |
| `ms_since_last_frame()` 超时判断 | 仅当帧过期超过 `stale_stream_timeout_ms`(3s) 才标记 degraded |
| 自动重连 + 可恢复 degraded | 帧恢复后自动清除 degraded，状态恢复为 running |
| BGR24 像素格式 | 避免 RGB/BGR 反转，与 OpenCV 默认配色空间一致 |

## 修改文件清单

### 新增文件
| 文件 | 说明 |
|------|------|
| `services/vision-event-module/include/ffmpeg_frame_reader.hpp` | FFmpegFrameReader 类声明 |
| `services/vision-event-module/src/ffmpeg_frame_reader.cpp` | FFmpegFrameReader 类实现（Windows CreateProcess + 管道读取） |

### 修改文件
| 文件 | 变更内容 |
|------|----------|
| `services/vision-event-module/src/service.cpp` | 移除 `cv::VideoCapture` 和 `capture_retry_after_ms`；替换为 `FFmpegFrameReader`；修复 degraded 状态语义（帧恢复后自动清除） |
| `services/vision-event-module/CMakeLists.txt` | 新增 `src/ffmpeg_frame_reader.cpp` 到编译源列表 |
| `services/vision-event-module/config/default_config.yaml` | 新增 `input.ffmpeg_path` 配置项 |

### 未修改文件（保护已有修复）
| 文件 | 原因 |
|------|------|
| `service.hpp` | 公共接口不变，`process_realtime_stream_frames()` 签名不变 |
| `frame_input.hpp` | InputFrame 结构不变，image 指针仍指向 cv::Mat data |
| `http_server.cpp` | HTTP API 不变 |
| 所有推理模块 | YOLO/ROI/事件算法逻辑完全不变 |
| 所有测试文件 | 测试仅依赖算法层，不依赖 RTSP 输入层 |

## 状态语义修复

### 旧行为
```cpp
// 帧读取失败 → 永久标记 degraded（不会恢复）
it->second.degraded = true;
```

### 新行为
```cpp
// 帧读取失败 → 仅在帧过期超时时标记 degraded
if (reader->ms_since_last_frame() > stale_stream_timeout_ms) {
    it->second.degraded = true;
}
// 双路帧均正常 → 清除 degraded
if (main_ok && aux_ok) {
    it->second.degraded = false;
}
```

### 状态转换
```
IDLE → (C服务启动) → RUNNING
  ├─ 帧缺失>3s → DEGRADED
  │   └─ 帧恢复 → RUNNING
  ├─ stop_match → STOPPED
  └─ (C服务stop) → STOPPED
```

## 配置说明

`default_config.yaml` 新增配置项：
```yaml
input:
  ffmpeg_path: "../../../third_party/windows/ffmpeg/bin/ffmpeg.exe"
```

- 相对路径基于配置文件所在目录（`services/vision-event-module/config/`）解析
- 运行时自动转换为绝对路径
- 可通过 HTTP API `/matches/init` 的 `stream_uri` 字段动态覆盖流地址

## 验证步骤

### 1. 构建验证
```bash
# MSVC 环境 + CMake + Ninja
cmake --build services/vision-event-module/build-ninja-msvc
```

### 2. 单元测试
```bash
ctest --test-dir services/vision-event-module/build-ninja-msvc --output-on-failure
# 要求: 5/5 通过
```

### 3. 实机 E2E 验证
1. 启动 A/B/C/D/E 所有服务
2. `POST /api/v1/matches` 创建比赛
3. `POST /api/v1/matches/{match_id}/start` 开始比赛
4. 等待 25 秒，让 C 模块充分拉流
5. `GET http://127.0.0.1:8083/api/v1/vision/matches/{match_id}/status`
   - 验收：`camera_main_status=online`, `camera_aux_status=online`, `status=running`
6. `POST /stop` 停止比赛
7. `POST /highlight` 生成集锦
8. 验收项：
   - ✅ C 运行中 `status=running`（非 degraded）
   - ✅ `focus_region_cam_01_ready=true`, `focus_region_cam_02_ready=true`
   - ✅ `event_candidates.json` 至少 1 条 `shot_candidate`
   - ✅ `full_highlight.mp4` 存在且可读
   - ✅ E highlight download 返回 200 video/mp4
   - ✅ stop 后无残留 ffmpeg 子进程（`tasklist | findstr ffmpeg`）

### 4. 残留进程检查
```powershell
# stop 后验证无残留 ffmpeg 进程
Get-Process -Name "ffmpeg" -ErrorAction SilentlyContinue
# 应无输出
```

## 风险与回滚

### 风险
1. **FFmpeg 子进程内存开销**：每个子进程约 30-50 MB，双路共 ~100 MB
2. **管道缓冲区**：Windows 默认管道缓冲区 4KB，rawvideo 帧 6.2MB/帧，需等待读取线程清空
3. **首次连接延迟**：FFmpeg 连 RTSP 约需 2-5 秒，期间 C 使用 fallback 模拟帧保证合同输出

### 回滚方案
如需回滚到 OpenCV VideoCapture 方案：
1. 恢复 `service.cpp` 中的 `captures` 和 `capture_retry_after_ms` 字段
2. 恢复 `ensure_capture_open()` 和原始 `read_stream_frame()` 实现
3. 从 CMakeLists.txt 移除 `src/ffmpeg_frame_reader.cpp`
4. 删除 `ffmpeg_frame_reader.hpp` 和 `ffmpeg_frame_reader.cpp`

### 已知限制
- 当前仅支持 Windows 平台（使用 Win32 CreateProcess / ReadFile API）
- FFmpeg 路径必须指向有效的 ffmpeg.exe
- 分辨率和帧率必须在配置中与 A 模块输出一致（1920x1080 @ 25fps）

---

## 状态

**✅ 已完成**

| 检查项 | 状态 |
|--------|------|
| 头文件编写 | ✅ `ffmpeg_frame_reader.hpp` |
| 实现文件编写 | ✅ `ffmpeg_frame_reader.cpp` |
| service.cpp 修改 | ✅ 移除 OpenCV VideoCapture，集成 FFmpegFrameReader |
| degraded 状态语义修复 | ✅ 帧恢复后自动清除 degraded |
| CMakeLists.txt 更新 | ✅ 新增源文件到编译列表 |
| default_config.yaml 更新 | ✅ 新增 ffmpeg_path 配置 |
| 编译通过 | ✅ MSVC 2022 + Ninja 10/10 目标 |
| CTest 通过 | ✅ 5/5 tests passed |
| 实机 E2E 验证 | ⏳ 待运行 |
