# 脚本使用指南

## 概述

本项目提供多组脚本文件，用于辅助开发、测试和部署 **GigE 双相机采集与编码分发模块**。架构采用 **双实例架构**：主相机推流到 `rtsp://127.0.0.1:8554/main`，辅相机推流到 `rtsp://127.0.0.1:8555/aux`。

## 快速上手（从 bin 目录）

所有脚本集中放置在 `services/ingest-streaming-module/bin/` 目录下。这是**推荐**运行目录。

### 步骤 1：编译项目

在 Visual Studio 2022 中打开 `ingest-streaming-module.slnx`，选择 **Release | x64** 配置，点击**生成** (Build)。

### 步骤 2：复制编译输出到 bin 目录

```batch
copy /y "x64\Release\ingest-streaming-module.exe" "bin\ingest-streaming-module.exe"
```

### 步骤 3：部署依赖文件

```batch
cd services\ingest-streaming-module\bin
deploy.bat
```

这会自动复制：
- GStreamer DLL 文件 (约 150 个)
- GStreamer 插件 DLL (约 200 个到 `lib\gstreamer-1.0\`)
- MVS SDK DLL (相机驱动)

### 步骤 4：验证部署

```batch
cd services\ingest-streaming-module\bin
dir *.exe
```

应包含：`ingest-streaming-module.exe`, `mediamtx.exe`, `ffmpeg.exe`, `*.dll`

### 步骤 5：启动相机推流

```batch
cd services\ingest-streaming-module\bin
start.bat
```

### 步骤 6：验证画面

在另一个 cmd 窗口中：

```batch
ffplay rtsp://127.0.0.1:8554/main
ffplay rtsp://127.0.0.1:8555/aux
```

或者打开 HLS 流：

```
http://127.0.0.1:8888/main
http://127.0.0.1:8889/aux
```

---

## bin 目录脚本说明

### `deploy.bat - 部署依赖

**作用**：将 GStreamer、MVS SDK 等所有运行时 DLL 复制到 bin 目录，使 bin 目录成为**自包含运行环境**。

**使用时机**：
- 首次部署时
- 更新了 GStreamer 或 MVS SDK 后
- 在同学电脑上首次运行前

**执行操作**：
1. 从 `third_party\gstreamer\bin\*.dll` 复制 DLL 到 bin
2. 从 `third_party\gstreamer\lib\gstreamer-1.0\*.dll` 复制插件到 `bin\lib\gstreamer-1.0\`
3. 从 `third_party\mvs_sdk\win64\*.dll` 复制 MVS SDK DLL 到 bin

**输出**：bin 目录包含约 350+ 个 DLL 文件，可独立运行，无需系统环境变量。

---

### `start.bat - 双实例启动（核心脚本

**作用**：启动完整的双实例采集推流服务。

**使用时机**：
- 日常开发调试
- 部署完成后的正式启动

**执行操作**：
1. 检查所有必需文件（mediamtx.exe / ingest-streaming-module.exe / ffmpeg.exe 以及相应配置文件）
2. 启动 MediaMTX 实例 1（监听 :8554，路径 /main）
3. 启动 MediaMTX 实例 2（监听 :8555，路径 /aux）
4. 启动 Ingest 采集模块（连接两台 GigE 相机进行 H.264 编码推流）

**端口分配**：

| 服务 | RTSP 端口 | HLS 端口 | 路径 | 相机 | 序列号 |
|------|-----------|----------|------|------|--------|
| MediaMTX 实例 1 | 8554 | 8888 | /main | 主相机 | F92514845 |
| MediaMTX 实例 2 | 8555 | 8889 | /aux | 辅相机 | D91363830 |
| Ingest HTTP API | - | 8081 | /api/v1/ingest/status | - | - |

**注意事项**：
- 确保相机已连接并上电（PoE 交换机或 GigE 供电）
- 确保相机序列号与 main.cpp 中配置一致
- 确保端口 8554、8555、8000-8003、8081 未被占用

---

### `test-local-video.bat - 本地视频测试脚本

**作用**：无需连接相机，使用 `test.mp4` 视频文件验证整个推流管道是否正常工作。

**使用时机**：
- 验证部署是否正确
- 在没有相机的环境下测试
- 测试 ffplay 播放能力

**执行操作**：
1. 启动双实例 MediaMTX（与生产配置相同）
2. ffmpeg 转码 test.mp4 → H.264，推送两个实例
3. 循环播放 test.mp4 (stream_loop -1)

**验证步骤**：
```batch
cd bin
test-local-video.bat
:: 另开窗口:
ffplay rtsp://127.0.0.1:8554/main
ffplay rtsp://127.0.0.1:8555/aux
```

---

## 完整文件清单（bin 目录应有）

```
bin/
├── ingest-streaming-module.exe  # 主程序 (编译后复制)
├── mediamtx.exe                 # RTSP 服务器 (自带)
├── ffmpeg.exe                   # FFmpeg 工具 (自带)
├── mediamtx_8554.yml           # 实例 1 配置
├── mediamtx_8555.yml           # 实例 2 配置
├── test.mp4                   # 测试视频 (自带)
├── start.bat                    # 启动脚本 (新建)
├── deploy.bat                   # 部署脚本 (新建)
├── test-local-video.bat         # 测试脚本 (新建)
├── *.dll                        # GStreamer + MVS SDK DLL (deploy.bat 复制)
└── lib/
    └── gstreamer-1.0/         # GStreamer 插件 (deploy.bat 复制)
```

---

## 第三方依赖说明

| 依赖 | 源路径 | 用途 | 数量 |
|------|---------|------|------|
| GStreamer | `third_party/gstreamer/bin/` | 视频编码、格式转换 | ~150 DLL |
| GStreamer 插件 | `third_party/gstreamer/lib/gstreamer-1.0/` | H.264 编码器、解析器等 | ~200 DLL |
| MVS SDK | `third_party/mvs_sdk/win64/` | 海康威视 GigE 相机 SDK | ~10 DLL |
| FFmpeg | `bin/ffmpeg.exe` | 从 GStreamer 读取并推送 RTSP | 1 EXE |
| MediaMTX | `bin/mediamtx.exe` | RTSP 流媒体服务器 | 1 EXE |

---

## 常见问题

### Q1: 启动后 ffplay 无法播放

**检查清单**：
1. `mediamtx.exe` 两个窗口是否在运行
2. `ingest-streaming-module.exe` 窗口是否有错误信息
3. 相机是否已连接（在 MVS 客户端能否看到相机）
4. 防火墙是否阻止了本地端口

### Q2: ingest-streaming-module.exe 启动时提示找不到相机

- 确认 MVS SDK DLL 是否正确复制到了 bin 目录
- 确认相机通过网线连接到电脑
- 确认相机 PoE 供电正常（相机指示灯状态）
- 确认序列号：F92514845 (主) / D91363830 (辅)

### Q3: 端口被占用

```batch
:: 查看端口占用
netstat -ano | findstr ":8554"
:: 杀掉占用进程
taskkill /F /PID <进程ID>
```

### Q4: 视频画面花屏或卡顿

- 检查 CPU 使用率（x264 编码为 CPU 密集型）
- 尝试降低分辨率或帧率（修改 main.cpp 中的相机配置）
- 检查网络带宽是否足够（双路 1080p@25fps 约需 4-8 Mbps）

### Q5: deploy.bat 找不到文件

- 确认 `third_party` 目录结构正确：
```
third_party/
├── gstreamer/
│   ├── bin/*.dll         ← 核心 DLL
│   ├── include/          ← 头文件 (编译用)
│   └── lib/gstreamer-1.0/*.dll  ← 插件 DLL
├── mvs_sdk/
│   ├── Includes/         ← 头文件
│   └── win64/*.dll       ← 运行时 DLL
└── ffmpeg/bin/ffmpeg.exe
```
