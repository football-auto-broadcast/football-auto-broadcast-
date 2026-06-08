# 脚本使用指南

## 概述

本项目包含多个脚本文件，用于辅助开发、测试和部署采集与编码分发模块。

## 脚本清单

### 1. `deploy.bat` - 部署脚本

**作用**：自动复制所有依赖文件到输出目录，确保项目可在其他电脑上运行。

**使用场景**：
- 在编译完成后运行，自动部署所有依赖
- 准备发布版本时使用
- 在同学电脑上首次运行前使用

**使用方法**：
```batch
cd services\ingest-streaming-module
deploy.bat
```

**执行步骤**：
1. 检查输出目录 `x64/Release/` 是否存在
2. 复制 GStreamer DLL 文件
3. 复制 GStreamer 插件
4. 复制 MVS SDK DLL
5. 复制配置文件和工具（mediamtx）
6. 复制 FFmpeg 工具

---

### 2. `start.bat` - 快速启动脚本

**作用**：在已部署的环境中快速启动所有服务。

**使用场景**：
- 日常开发调试时快速启动服务
- 在部署完成后启动服务

**使用方法**：
```batch
cd services\ingest-streaming-module\x64\Release
start.bat
```

**执行步骤**：
1. 检查 `mediamtx.exe` 是否存在
2. 检查 `ingest-streaming-module.exe` 是否存在
3. 启动 MediaMTX RTSP 服务器
4. 启动采集模块

**注意**：此脚本使用**单实例配置**（端口 8554，双路径 main/aux），适用于生产环境。

---

### 3. `copy_dlls.bat` - DLL 手动复制工具

**作用**：辅助手动复制 DLL 文件，提供交互式界面。

**使用场景**：
- 需要手动选择特定 DLL 文件时使用
- 调试依赖问题时使用

**使用方法**：
```batch
cd services\ingest-streaming-module
copy_dlls.bat
```

---

### 4. `test_dual_instance.ps1` - 双实例测试脚本

**作用**：启动两个 MediaMTX 实例并推送测试视频，验证双实例功能。

**使用场景**：
- 测试双实例 RTSP 服务器功能
- 验证两个相机通道同时工作
- 测试本地视频推流

**使用方法**：
```powershell
cd services\ingest-streaming-module
.\test_dual_instance.ps1
```

**执行步骤**：
1. 检查测试视频文件 `bin/test.mp4`
2. 检查项目内的 FFmpeg（`third_party/ffmpeg/bin/ffmpeg.exe`）
3. 清理旧进程（避免端口冲突）
4. 启动实例1（端口 8554，路径 main）
5. 启动实例2（端口 8555，路径 aux）
6. 推送视频到两个实例

**测试结果**：
| 实例 | RTSP 地址 | HLS 地址 |
|------|-----------|----------|
| 实例1 | rtsp://127.0.0.1:8554/main | http://127.0.0.1:8888/main |
| 实例2 | rtsp://127.0.0.1:8555/aux | http://127.0.0.1:8889/aux |

---

## 使用流程

### 开发环境搭建
```batch
# 1. 编译项目（Release + x64）
# 2. 运行部署脚本
cd services\ingest-streaming-module
deploy.bat
# 3. 启动服务（单实例模式）
cd x64/Release
start.bat
```

### 双实例功能测试
```powershell
cd services\ingest-streaming-module
.\test_dual_instance.ps1
```

### 验证方法
```powershell
# 使用 ffplay 播放
ffplay rtsp://127.0.0.1:8554/main
ffplay rtsp://127.0.0.1:8555/aux

# 或使用浏览器打开 HLS 流
# http://127.0.0.1:8888/main
# http://127.0.0.1:8889/aux
```

---

## 目录结构

```
services/ingest-streaming-module/
├── deploy.bat              # 部署脚本
├── start.bat               # 快速启动脚本
├── copy_dlls.bat           # DLL 复制工具
├── test_dual_instance.ps1  # 双实例测试脚本
├── bin/
│   ├── mediamtx.exe        # MediaMTX 可执行文件
│   ├── mediamtx.yml        # 单实例配置
│   ├── mediamtx2.yml       # 单实例双路径配置
│   ├── mediamtx_8554.yml   # 双实例配置1
│   ├── mediamtx_8555.yml   # 双实例配置2
│   └── test.mp4            # 测试视频
└── x64/
    └── Release/            # 编译输出目录
        ├── ingest-streaming-module.exe
        ├── mediamtx.exe
        ├── mediamtx.yml
        ├── ffmpeg.exe
        ├── ffplay.exe
        ├── ffprobe.exe
        └── lib/
            └── gstreamer-1.0/
```

---

## 第三方依赖

| 依赖 | 路径 | 用途 |
|------|------|------|
| GStreamer | `third_party/gstreamer/` | 核心流媒体处理 |
| MVS SDK | `third_party/mvs_sdk/` | 相机驱动 |
| FFmpeg | `third_party/ffmpeg/` | 测试视频推流 |
| MediaMTX | `services/ingest-streaming-module/bin/` | RTSP 服务器 |

---

## 注意事项

1. **FFmpeg 依赖**：测试脚本使用项目内的 FFmpeg，无需系统安装
2. **管理员权限**：某些脚本可能需要管理员权限才能复制文件
3. **端口冲突**：确保端口 8554、8555、8888、8889 未被其他程序占用
4. **测试视频**：测试脚本需要 `bin/test.mp4` 文件存在
5. **部署顺序**：先编译项目，再运行 deploy.bat，最后运行 start.bat