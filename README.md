# football-auto-broadcast-足球赛事自动转播&精彩集锦生成系统
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/github/actions/workflow/status/your-org/football-auto-broadcast/build.yml?branch=main&label=build)  
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/github/actions/workflow/status/your-org/football-auto-broadcast/test.yml?branch=main&label=tests)  
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/github/license/your-org/football-auto-broadcast)  
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/badge/C%2B%2B-17-blue)  
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/badge/status-MVP-orange)  
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/badge/architecture-single--host-green)

一个面向校队比赛与训练赛的轻量、可移动部署的足球自动转播与精彩集锦生成系统。

---

## 项目简介
足球比赛的视频采集与精彩集锦制作通常依赖大量人工，流程繁琐、耗时较长。  
本项目旨在构建一套低成本、可移动部署的足球赛事自动转播系统，支持以下能力：

+ 接入两路工业相机视频
+ 全场原始视频录制
+ 实时输出基础自动转播主画面
+ 识别进球和精彩片段候选事件
+ 赛后自动生成全场精彩集锦
+ 提供比赛控制、状态查看和结果下载后台

本系统适用于校队比赛、训练赛、校园杯等中小型足球场景。

---

## MVP 范围
第一版 MVP 聚焦于打通一条稳定的端到端闭环流程。

### MVP 已包含功能
+ 两路工业相机视频接入
+ 原始视频实时录制
+ 基于主机位的基础自动转播主画面生成
+ 进球候选事件识别
+ 精彩片段候选识别
+ 赛后自动生成全场精彩集锦
+ 基于 Git 的团队协作开发
+ 单主机集中式部署

### 未来迭代功能
+ 自研无线图传
+ 复杂多机位自动切镜
+ 球员号码识别
+ 个人集锦生成
+ 全自动跨镜头球员身份统一
+ 战术分析与高级统计
+ 手机 App
+ 公网直播与云端部署

---

## 硬件方案
当前项目硬件方案如下：

+ 工业相机：海康 GigE 工业相机 2 台
+ 镜头：6mm 或 8mm 广角镜头 2 只
+ 交换机：千兆 PoE 交换机 1 台
+ 主机：RTX 4080 主机 1 台
+ 网线：Cat6 六类网线若干
+ 支架与防护：根据场地实际部署补充

---

## 系统架构
系统采用 **单主机集中式处理架构**。

<!-- 这是一个文本绘图，源码为：flowchart LR

    CAM1["工业相机1\nGigE\n主机位"]
    CAM2["工业相机2\nGigE\n辅机位"]

    SW["千兆 PoE 交换机"]

    HOST["主机 PC\nRTX 4080\n统一采集 / 录制 / 分析 / 集锦生成"]

    VIEW["可选控制端\n笔记本或普通 PC\n用于网页控制与结果查看"]

    CAM1 --> SW
    CAM2 --> SW
    SW --> HOST
    HOST --> VIEW -->
![](https://cdn.nlark.com/yuque/__mermaid_v3/15da9b57663a5a072f103ebb1dda2811.svg)
 

### 核心思路
+ 两台工业相机通过 GigE 接入千兆 PoE 交换机
+ 主机通过工业相机 SDK 统一取流
+ 主机统一完成录制、主画面生成、视觉分析、候选事件识别与集锦生成
+ 可选控制端通过浏览器访问后台页面，完成比赛控制与结果查看

 

## 通信架构
<!-- 这是一个文本绘图，源码为：flowchart TB

 
## 部署拓扑
+ **工业相机 1**
    - 主机位
    - 全场覆盖
+ **工业相机 2**
    - 辅机位
    - 补充画面与素材，辅助识别
+ **主机 PC**
    - 工业相机取流
    - 原始录像
    - 自动转播主画面
    - 视觉分析
    - 集锦生成
    - 平台服务
+ **可选控制端**
    - 访问后台页面
    - 查看主画面预览
    - 查看集锦结果
    - 下载输出文件

---

## **主要流程**
### **比赛前**
1. 架设两台工业相机
2. 连接千兆 PoE 交换机
3. 主机加载工业相机 SDK 并初始化取流
4. 在平台中创建比赛任务


    subgraph L1["硬件层"]
        CAM1["工业相机1<br/>GigE / 主机位"]
        CAM2["工业相机2<br/>GigE / 辅机位"]
        SW["千兆 PoE 交换机"]
    end
    
    subgraph L2["接入层"]
        A["ingest-streaming-module<br/>采集与编码分发"]
    end
    
    subgraph L3["处理层"]
        B["record-program-module<br/>录像与主画面生成"]
        C["vision-event-module<br/>视觉分析"]
        D["highlight-generation-module<br/>事件与集锦生成"]
    end
    
    subgraph L4["存储层"]
        RAW["raw<br/>原始录像"]
        PROGRAM["program<br/>主画面录像"]
        META["metadata<br/>候选事件数据"]
        OUTPUT["output<br/>集锦输出"]
        LOGS["logs<br/>运行日志"]
    end
    
    subgraph L5["平台层"]
        E["platform-orchestration-module<br/>平台与调度"]
        VIEW["浏览器 / 可选控制端"]
    end
    
    CAM1 --> SW
    CAM2 --> SW
    SW --> A
    
    A --> B
    A --> C
    
    C -- "关注区域" --> B
    C -- "候选事件" --> D
    
    B --> RAW
    B --> PROGRAM
    C --> META
    D --> OUTPUT
    
    A --> LOGS
    B --> LOGS
    C --> LOGS
    D --> LOGS
    E --> LOGS
    
    RAW --> D
    PROGRAM --> D
    META --> D
    
    E -- "HTTP 控制" --> A
    E -- "HTTP 控制" --> B
    E -- "HTTP 控制" --> C
    E -- "HTTP 控制" --> D
    
    B -- "主画面预览" --> E
    D -- "任务状态 / 输出路径" --> E
    
    VIEW --> E -->

![](https://cdn.nlark.com/yuque/__mermaid_v3/f4a431362e0be3e464a45c50791ca64b.svg)

--- 

### 核心思路
+ 两台工业相机通过 GigE 接入千兆 PoE 交换机
+ 主机通过工业相机 SDK 统一取流
+ 主机统一完成录制、主画面生成、视觉分析、候选事件识别与集锦生成
+ 可选控制端通过浏览器访问后台页面，完成比赛控制与结果查看

---

## 部署拓扑
+ **工业相机 1**
    - 主机位
    - 全场覆盖
+ **工业相机 2**
    - 辅机位
    - 补充画面与素材，辅助识别
+ **主机 PC**
    - 工业相机取流
    - 原始录像
    - 自动转播主画面
    - 视觉分析
    - 集锦生成
    - 平台服务
+ **可选控制端**
    - 访问后台页面
    - 查看主画面预览
    - 查看集锦结果
    - 下载输出文件

---

## **主要流程**
### **比赛前**
1. 架设两台工业相机
2. 连接千兆 PoE 交换机
3. 主机加载工业相机 SDK 并初始化取流
4. 在平台中创建比赛任务

 develop
### **比赛中**
1. 开始比赛录制
2. 录制两路原始视频
3. 输出一路自动转播主画面
4. 视觉模块持续生成关注区域和候选事件数据

### **比赛后**
1. 结束比赛录制
2. 读取录像文件与候选事件
3. 生成全场精彩集锦
4. 在平台中查看并下载集锦结果

---

## **技术栈**
### **核心语言**
+ C++
+ Python，用于训练脚本和离线工具

### **多媒体处理**
+ GStreamer
+ FFmpeg
+ OpenCV

### **AI 推理**
+ ONNX Runtime
+ TensorRT

### **工业相机接入**
+ 相机厂商 SDK
+ GigE 取流
+ 本地编码与文件写入

### **协作方式**
+ Git
+ Pull Request 工作流

---
## 目录结构
football-auto-broadcast/
├── docker/
│   ├── windows/
│   │   ├── common/
│   │   │   └── install_runtime.ps1
│   │   ├── record-program-module/
│   │   │   └── Dockerfile.windows
│   │   ├── vision-event-module/
│   │   │   └── Dockerfile.windows
│   │   ├── highlight-generation-module/
│   │   │   └── Dockerfile.windows
│   │   ├── platform-orchestration-module/
│   │   │   └── Dockerfile.windows
│   │   └── ingest-streaming-module/
│   │       └── Dockerfile.windows
├── services/
│   ├── ingest-streaming-module/
│   ├── record-program-module/
│   ├── vision-event-module/
│   ├── highlight-generation-module/
│   └── platform-orchestration-module/
└── third_party/
    └── windows/
        ├── gstreamer/
        ├── opencv/
        ├── onnxruntime/
        └── vc_redist/

## **逻辑模块划分**
虽然运行架构已经改为单主机集中式处理，但工程实现上仍然保留模块化设计，便于多人协作与后续扩展。

### **ingest-streaming-module**
+ 工业相机接入
+ 工业相机 SDK 取流
+ 视频编码压缩
+ 视频流分发
+ 流状态上报

### **record-program-module**
+ 原始录像保存
+ 自动转播主画面生成
+ 主画面录制
+ 文件归档与索引

### **vision-event-module**
+ 足球基础检测
+ 高活跃区域分析
+ 主画面关注区域输出
+ 进球候选识别
+ 精彩片段候选识别

### **highlight-generation-module**
+ 候选事件解析
+ 视频片段裁切
+ 全场精彩集锦生成
+ 集锦导出

### **platform-orchestration-module**
+ 比赛生命周期管理
+ 各模块任务编排
+ 任务状态跟踪
+ 后台管理页面
+ 主画面预览
+ 集锦结果下载
+ 配置与日志管理

---

## **项目特点**
+ **低成本可落地**  
采用双工业相机 + 单主机集中式处理，降低系统联调复杂度。
+ **关注 MVP 可落地性**  
聚焦自动转播主画面和全场精彩集锦两条主线，控制项目复杂度。
+ **工业相机适配**  
系统针对 GigE 工业相机链路设计，适合固定机位和稳定采集场景。
+ **适合 RTX 4080 主机**  
主机性能足以承担双路采集、录像、主画面生成、视觉分析和集锦输出。
+ **模块职责清晰**  
逻辑模块边界明确，便于多人并行开发与后续扩展。

---

## **当前状态**
当前仓库处于 MVP 设计与实现阶段。

计划中的首个公开里程碑包括：

+ 双工业相机视频接入
+ 自动转播主画面输出
+ 进球与精彩片段候选识别
+ 赛后全场精彩集锦生成
+ 平台统一控制与结果下载

---

## **适用场景**
本项目当前最适合以下场景：

+ 校队训练赛
+ 校园足球比赛
+ 固定机位赛事记录
+ 赛后精彩集锦快速生成

当前阶段不面向：

+ 商业级大型直播平台
+ 高并发公网直播
+ 多机位复杂导播系统
+ 球员身份精细分析系统

---

---

## 项目自包含性说明

本项目采用完全自包含（Self-Contained）的构建设计，**无需额外安装任何第三方依赖**。所有构建与运行所需文件均已纳入仓库，拿到代码即可编译与运行。

### 已纳入的第三方组件

| 组件 | 用途 | 位置 |
|------|------|------|
| 海康威视 MVS SDK (Win64) | GigE 工业相机驱动与取流 | `third_party/mvs_sdk/` |
| GStreamer 1.x (Win64) | 视频管道构建、H.264 编码、RTSP 推流 | `third_party/gstreamer/` |
| FFmpeg (Win64) | 视频录制与本地测试工具 | `third_party/ffmpeg/` |
| MediaMTX v1.8.2 (Win64) | RTSP 流媒体服务器（双实例） | `services/ingest-streaming-module/bin/` |

### 构建依赖

- **编译器**：Microsoft Visual C++ 2022 (MSVC v143) / Visual Studio 2022 社区版
- **工具集**：Platform Toolset v145，Windows SDK 10.0
- **语言标准**：C++20 (`/std:c++20`)
- **构建系统**：MSBuild（随 Visual Studio 2022 一同安装）

**说明**：项目中 `.vcxproj` 文件通过相对路径直接引用 `third_party/` 下的头文件与 `.lib`，无需配置系统级环境变量。运行时所需的所有 `.dll` 均已拷贝至 `bin/` 目录，随可执行文件一同发布。

---

## 目录结构

```
football-auto-broadcast-/
├── README.md                                      # 本文件
├── LICENSE
├── services/
│   └── ingest-streaming-module/                   # 当前 MVP 唯一已实现模块
│       ├── src/                                   # 源文件（.cpp）
│       │   ├── main.cpp                           # 程序入口
│       │   ├── camera_device.cpp                  # 相机设备：Bayer→RGB 转换、帧获取
│       │   ├── gst_rtsp_streamer.cpp              # GStreamer 推流管道：appsrc→x264→rtspclientsink
│       │   └── ingest_engine.cpp                  # 引擎：双相机调度 + 双推流线程
│       ├── include/                               # 头文件（.h / .hpp）
│       │   ├── camera_device.h
│       │   ├── gst_rtsp_streamer.h
│       │   ├── httplib.h                          # 内嵌 HTTP 客户端库（C++ header-only）
│       │   └── ingest_engine.h
│       ├── bin/                                   # 运行目录（所有可执行文件与 .dll 都在这里）
│       │   ├── ingest_streaming_service.exe        # 编译产物（构建后生成）
│       │   ├── mediamtx.exe                       # MediaMTX v1.8.2 RTSP 服务器
│       │   ├── mediamtx_8554.yml                  # 主相机 MediaMTX 实例配置（端口 8554）
│       │   ├── mediamtx_8555.yml                  # 辅相机 MediaMTX 实例配置（端口 8555）
│       │   ├── ffmpeg.exe / ffplay.exe / ffprobe.exe
│       │   ├── *.dll                              # GStreamer + MVS SDK 所有运行时库
│       │   ├── lib/gstreamer-1.0/                 # GStreamer 插件目录
│       │   └── test.mp4                           # 本地视频测试素材
│       ├── tool/                                  # 辅助工具
│       │   └── camera_enum.cpp                    # 相机枚举工具源码
│       ├── ingest-streaming-module.vcxproj        # Visual Studio 项目文件（含第三路径配置）
│       ├── ingest-streaming-module.slnx           # Visual Studio 解决方案文件
│       ├── copy_dlls.bat                          # 第三方 DLL 拷贝脚本（从 third_party/ 拷贝到 bin/）
│       ├── start.bat                              # 一键启动脚本
│       └── deploy.bat                             # 部署脚本
└── third_party/                                   # 所有第三方依赖（构建 + 运行时）
    ├── mvs_sdk/                                   # 海康 MVS SDK
    │   ├── Includes/                              # 头文件（MvCameraControl.h 等）
    │   └── win64/                                 # MvCameraControl.lib + .dll
    ├── gstreamer/                                 # GStreamer 完整分发
    │   ├── include/                               # 头文件（gstreamer-1.0/、glib-2.0/ 等）
    │   ├── lib/                                   # 链接库（.lib）
    │   └── bin/                                   # 可执行文件与 .dll（由 copy_dlls.bat 拷贝到 bin/）
    └── ffmpeg/                                    # FFmpeg 工具
        └── bin/                                   # ffmpeg.exe、ffplay.exe、ffprobe.exe
```

---

## 构建指南

### 前置条件

1. 安装 **Visual Studio 2022 社区版**（免费）：
   - 工作负荷：**使用 C++ 的桌面开发**（Desktop Development with C++）
   - 自动附带：MSBuild、Windows 10/11 SDK、Platform Toolset v143/v145
2. 克隆或下载本仓库到本地。

### 构建步骤

**方式 A：Visual Studio IDE**
1. 双击打开 `services/ingest-streaming-module/ingest-streaming-module.slnx`
2. 在顶部工具栏选择 **Release + x64**
3. 菜单 → **生成** → **生成解决方案**（快捷键 F7）
4. 编译产物：`services/ingest-streaming-module/x64/Release/ingest_streaming_service.exe`
5. 将编译产物复制到运行目录：
   ```
   复制 x64\Release\ingest_streaming_service.exe → bin\ingest_streaming_service.exe
   ```

**方式 B：命令行（MSBuild）**
```bat
cd services\ingest-streaming-module
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ingest-streaming-module.slnx /p:Configuration=Release /p:Platform=x64 /t:Rebuild /v:minimal
copy x64\Release\ingest_streaming_service.exe bin\ingest_streaming_service.exe
```

### 构建成功验证

- `x64\Release\ingest_streaming_service.exe` 文件存在
- 无编译错误（Build: 1 succeeded, 0 failed）

**说明**：项目已配置好所有第三方依赖的相对路径（`include/`、`lib/`），**无需修改任何项目属性或设置环境变量**。

---

## 运行指南

### 双实例架构概览

本项目为每台相机分配独立的 MediaMTX（RTSP 服务器）实例，实现资源隔离与故障隔离：

```
┌────────────────────────────────────────────────────────────────────┐
│                       ingest_streaming_service.exe                   │
│                                                                    │
│   ┌──────────────────┐          ┌──────────────────┐              │
│   │ 相机 1 (Cam0)    │  帧      │ GStreamer 推流 1  │ appsrc      │
│   │ GigE, 2592x1944  │ ───────▶ │ x264 → RTSP       │ ───┐       │
│   │ (Bayer8, GR)     │          │ rtsp://127.0.0.1:8554/main      │
│   └──────────────────┘          └──────────────────┘        │     │
│                                                               ▼    │
│   ┌──────────────────┐          ┌──────────────────┐   ┌────────┐ │
│   │ 相机 2 (Cam1)    │  帧      │ GStreamer 推流 2  │   │ MTX   │ │ 端口 8554
│   │ GigE, 2592x1944  │ ───────▶ │ x264 → RTSP       │──▶│ 8554  │ │ → rtsp://127.0.0.1:8554/main
│   │ (Bayer12, BG)    │          │ rtsp://127.0.0.1:8555/aux      │
│   └──────────────────┘          └──────────────────┘        │     │
│                                                               ▼    │
│                                                            ┌────┐ │
│                                                            │ MTX │ │ 端口 8555
│                                                            │ 8555 │ │ → rtsp://127.0.0.1:8555/aux
│                                                            └────┘ │
└────────────────────────────────────────────────────────────────────┘

         ▲                         ▲                     ▲
         │ 浏览器访问               │ ffplay 播放          │ ffplay 播放
         │ http://127.0.0.1:8081   │ rtsp://...:8554/main │ rtsp://...:8555/aux
```

**架构优势**：
- 每路推流独立运行，一台相机断线不影响另一台
- 两个 MediaMTX 实例端口分离，便于独立调试
- GStreamer pipeline 参数统一，画质可集中调优

### 启动步骤

1. **确保构建完成**：`bin/ingest_streaming_service.exe` 已存在
2. **确保两台 GigE 相机已上电并连接到同一台主机的千兆网卡**
3. **进入 bin 目录，启动主程序**：
   ```bat
   cd services\ingest-streaming-module\bin
   .\ingest_streaming_service.exe
   ```
4. **在另一个终端（或 ffplay GUI）播放主相机画面**：
   ```bat
   ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/main -window_title MAIN_CAM -x 720 -y 540
   ```
5. **在第三个终端（或 ffplay GUI）播放辅相机画面**：
   ```bat
   ffplay -rtsp_transport tcp rtsp://127.0.0.1:8555/aux -window_title AUX_CAM -x 720 -y 540
   ```
6. **查看运行状态**：浏览器访问 `http://127.0.0.1:8081/api/v1/ingest/status`

### 本地视频测试（无相机环境下验证架构）

```bat
:: 在一个终端启动 MediaMTX 8554 实例
mediamtx mediamtx_8554.yml

:: 在另一个终端推流测试视频
ffmpeg -re -stream_loop -1 -i test.mp4 -c:v libx264 -preset ultrafast -f rtsp rtsp://127.0.0.1:8554/main

:: 在第三个终端播放
ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/main
```

### GStreamer 推流管道参数

当前使用的推流管道（在 `gst_rtsp_streamer.cpp` 中构建）：
```
appsrc name=mysrc caps=video/x-raw,format=RGB,width=2592,height=1944,framerate=25/1
  ! videoconvert
  ! videoscale method=1        # Lanczos 缩放，保持画质
  ! video/x-raw,format=I420,width=1920,height=1080,framerate=25/1
  ! x264enc tune=zerolatency speed-preset=medium bitrate=20000 qp-min=10 qp-max=30 key-int-max=25
  ! h264parse config-interval=1
  ! video/x-h264,stream-format=byte-stream
  ! rtspclientsink protocols=tcp latency=0 location=rtsp://127.0.0.1:<PORT>/<PATH>
```

**参数说明**：
- `bitrate=20000`：20 Mbps 目标码率，在 1080p@25fps 下提供清晰画质
- `speed-preset=medium`：编码速度与质量平衡（非实时录像可改为 slow/slower）
- `qp-min=10 qp-max=30`：限制量化参数范围，防止局部马赛克
- `key-int-max=25`：每秒一个关键帧（GOP=25），降低播放器启动延迟

### Bayer 格式转换说明

两台相机输出不同原始格式：
- **主相机 (Cam0, F92514845)**：Bayer8, GR pattern（Green-Red 起始）
- **辅相机 (Cam1, D91363830)**：Bayer12, BG pattern（Blue-Green 起始），12 位原始值先线性缩放到 8 位，再应用自动对比度拉伸（Auto-Contrast Stretch），最后走 2x2 块 demosaic

---

## 无用/冗余文件清单（建议清理）

以下文件为开发过程中遗留的临时脚本、旧配置或测试产物，**不参与构建与运行逻辑**，可安全删除以精简仓库：

### `services/ingest-streaming-module/bin/` 目录

| 文件 | 类型 | 说明 | 建议 |
|------|------|------|------|
| `mediamtx.yml` | 旧配置 | 单实例默认配置，已被 `mediamtx_8554.yml` + `mediamtx_8555.yml` 双实例替代 | ✅ 删除 |
| `mediamtx2.yml` | 旧配置 | 早期双实例尝试版本，与当前 `mediamtx_8555.yml` 功能重复 | ✅ 删除 |
| `football-...-stream_main.h264` | 测试产物 | 开发过程中生成的裸 H.264 测试流，运行时不读取 | ✅ 删除 |
| `football-...-stream_aux.h264` | 测试产物 | 同上 | ✅ 删除 |
| `check.ps1` | 旧脚本 | 早期诊断脚本，功能被程序内置输出替代 | ✅ 删除 |
| `check_analysis.ps1` | 旧脚本 | 同上 | ✅ 删除 |
| `check_files.ps1` | 旧脚本 | 文件校验脚本 | ✅ 删除 |
| `check_gst.ps1` | 旧脚本 | GStreamer 检查脚本 | ✅ 删除 |
| `check_running.ps1` | 旧脚本 | 运行状态检查脚本 | ✅ 删除 |
| `deploy.bat`（bin 目录下） | 旧脚本 | 与项目根目录 `deploy.bat` 重复 | ✅ 删除 |
| `deploy_exe.ps1` | 旧脚本 | EXE 部署辅助脚本 | ✅ 删除 |
| `deploy_helper.ps1` | 旧脚本 | 同上 | ✅ 删除 |
| `diagnose.ps1` | 旧脚本 | 诊断脚本 | ✅ 删除 |
| `launch_ingest.bat` | 旧脚本 | 启动脚本旧版本 | ✅ 删除 |
| `run_ingest.ps1` | 旧脚本 | 同上 | ✅ 删除 |
| `stop_local.ps1` | 旧脚本 | 停止脚本 | ✅ 删除 |
| `stop_local2.ps1` | 旧脚本 | 同上 | ✅ 删除 |
| `stop_local3.ps1` | 旧脚本 | 同上 | ✅ 删除 |
| `test_camera_v2.ps1` | 旧脚本 | 相机测试脚本 | ✅ 删除 |
| `test_gst.bat` | 旧脚本 | GStreamer 测试脚本 | ✅ 删除 |
| `test_gst.ps1` | 旧脚本 | 同上 | ✅ 删除 |

### `services/ingest-streaming-module/` 根目录

| 文件 | 类型 | 说明 | 建议 |
|------|------|------|------|
| `test_dual_instance.ps1` | 旧脚本 | 双实例测试脚本 | ✅ 删除 |
| `ingest-streaming-module.vcxproj.user` | VS 用户配置 | 仅包含个人 VS 设置（断点、窗口布局等），建议加入 `.gitignore` 而非提交 | 加入 `.gitignore` |

### `x64/` 目录（构建输出）

| 目录 | 类型 | 说明 | 建议 |
|------|------|------|------|
| `x64/Release/`、`x64/Debug/` | 构建产物 | MSBuild 输出目录，包含 `.obj`、`.pdb`、`.exe` 等临时文件 | 加入 `.gitignore`（仅将最终 `.exe` 复制到 `bin/` 后提交） |

### 清理后保留的核心文件

```
bin/
├── ingest_streaming_service.exe     # 主程序（构建后复制）
├── mediamtx.exe                    # MediaMTX v1.8.2
├── mediamtx_8554.yml               # 主相机 RTSP 配置（端口 8554）
├── mediamtx_8555.yml               # 辅相机 RTSP 配置（端口 8555）
├── ffmpeg.exe / ffplay.exe / ffprobe.exe
├── CommonParameters.ini             # MVS SDK 配置
├── MvDSS.ax / MvDSS2.ax            # MVS SDK DirectShow 插件
├── test.mp4                         # 本地视频测试素材
├── *.dll                            # 运行时依赖（GStreamer + MVS SDK）
└── lib/gstreamer-1.0/               # GStreamer 插件
```

---

## 常见问题

### Q1：构建报 "找不到 MvCameraControl.h" 或 "无法解析的外部符号 MvCamera_XXX"
**A**：检查 `vcxproj` 中 `AdditionalIncludeDirectories` 和 `AdditionalLibraryDirectories` 的相对路径是否正确指向 `$(ProjectDir)..\..\third_party\mvs_sdk\Includes` 和 `...\win64`。本仓库中已配置好，一般无需修改。

### Q2：启动后相机初始化失败，日志报 "No camera found"
**A**：
1. 确认相机已上电，网线正常（交换机指示灯亮）
2. 在海康 MVS 客户端中先确认相机可被枚举到
3. 检查 `bin/CommonParameters.ini` 是否存在且未被篡改
4. 确保 `bin/` 目录下存在 MVS SDK 的 `.dll`（`MvCameraControl.dll`、`MVGigEVisionSDK.dll` 等）

### Q3：ffplay 连接 RTSP 超时或花屏
**A**：
1. 确保 `mediamtx.exe` 两个实例都已启动（默认由主程序自动管理）
2. 播放时必须加 `-rtsp_transport tcp` 参数（UDP 在本地回环有时丢包）
3. 花屏/马赛克通常是 `x264enc` 的 `qp-max` 设置过大，当前已设为 30，可降低到 26 以强制更高质量（代价：码率上升）

### Q4：仅想测试架构，没有相机怎么办？
**A**：按照"本地视频测试"小节，使用 `test.mp4` 通过 `ffmpeg` 推流来验证 MediaMTX + RTSP 链路。

---

## 联系方式
如有问题、功能建议或合作需求，请通过 Issue 或 Pull Request 联系。

