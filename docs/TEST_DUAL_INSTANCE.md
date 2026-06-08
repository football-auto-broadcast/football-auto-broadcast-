# 双实例测试文档

## 概述

本文档描述如何测试 MediaMTX 双实例功能，实现两个独立的 RTSP 流推送。

## 测试结果

| 测试项 | 结果 |
|--------|------|
| 单实例启动 | ✅ 通过 |
| 单实例推流 | ✅ 通过 |
| 双实例启动 | ✅ 通过 |
| 双实例推流 | ✅ 通过 |

## 测试环境

- **操作系统**: Windows 10/11 (x64)
- **MediaMTX 版本**: v1.8.2
- **FFmpeg**: v8.1 (项目内置)

## 配置文件

### 单实例配置 (mediamtx2.yml)

```yaml
rtspAddress: :8554
rtsp: yes
rtpAddress: :8000
rtcpAddress: :8001
hls: yes
hlsAddress: :8888
rtmp: no
webrtc: no
srt: no
paths:
  main:
    source: publisher
  aux:
    source: publisher
```

### 双实例配置1 (mediamtx_8554.yml)

```yaml
rtspAddress: :8554
rtsp: yes
rtpAddress: :8000
rtcpAddress: :8001
hls: yes
hlsAddress: :8888
rtmp: no
webrtc: no
srt: no
paths:
  main:
    source: publisher
```

### 双实例配置2 (mediamtx_8555.yml)

```yaml
rtspAddress: :8555
rtsp: yes
rtpAddress: :8010
rtcpAddress: :8011
hls: yes
hlsAddress: :8889
rtmp: no
webrtc: no
srt: no
paths:
  aux:
    source: publisher
```

## 端口分配

| 实例 | RTSP端口 | RTP端口 | RTCP端口 | HLS端口 |
|------|----------|---------|----------|---------|
| 实例1 | 8554 | 8000 | 8001 | 8888 |
| 实例2 | 8555 | 8010 | 8011 | 8889 |

## 测试方法

### 方法一：使用测试脚本（推荐）

```powershell
cd services\ingest-streaming-module
.\test_dual_instance.ps1
```

脚本执行流程：
1. 检查测试视频文件 (`bin/test.mp4`)
2. 检查 FFmpeg（`third_party/ffmpeg/bin/ffmpeg.exe`）
3. 清理旧进程（避免端口冲突）
4. 启动两个 MediaMTX 实例
5. 推送测试视频到两个实例
6. 验证进程运行状态

### 方法二：手动测试

**步骤1：启动实例1**
```cmd
cd services\ingest-streaming-module\bin
mediamtx.exe mediamtx_8554.yml
```

**步骤2：启动实例2**
```cmd
cd services\ingest-streaming-module\bin
mediamtx.exe mediamtx_8555.yml
```

**步骤3：推送视频到实例1**
```cmd
ffmpeg -re -i bin/test.mp4 -c:v libx264 -preset ultrafast -tune zerolatency -f rtsp rtsp://127.0.0.1:8554/main
```

**步骤4：推送视频到实例2**
```cmd
ffmpeg -re -i bin/test.mp4 -c:v libx264 -preset ultrafast -tune zerolatency -f rtsp rtsp://127.0.0.1:8555/aux
```

## 验证方法

### 方法1：使用 ffplay

```powershell
# 播放主流
ffplay rtsp://127.0.0.1:8554/main

# 播放辅流
ffplay rtsp://127.0.0.1:8555/aux
```

### 方法2：使用浏览器（HLS）

| 流 | URL |
|----|-----|
| 主流 | http://127.0.0.1:8888/main |
| 辅流 | http://127.0.0.1:8889/aux |

### 方法3：检查进程

```powershell
Get-Process -Name mediamtx
Get-Process -Name ffmpeg
```

## 预期结果

| 检查项 | 预期结果 |
|--------|----------|
| MediaMTX实例数 | 2个 |
| FFmpeg实例数 | 2个 |
| 实例1状态 | 正常运行，无错误 |
| 实例2状态 | 正常运行，无错误 |
| 主流播放 | 正常播放视频 |
| 辅流播放 | 正常播放视频 |

## 已修复的问题

### 1. 配置文件字段错误

**问题**：使用 `httpAddress` 字段导致 MediaMTX 启动失败

**修复**：改用正确的 `hlsAddress` 字段

### 2. 端口冲突

**问题**：RTMP、WebRTC、SRT 端口冲突导致双实例启动失败

**修复**：禁用不需要的协议（rtmp: no, webrtc: no, srt: no）

### 3. FFmpeg 参数格式错误

**问题**：PowerShell 中 `Start-Process` 参数格式错误导致 FFmpeg 未启动

**修复**：使用数组格式传递参数
```powershell
$ffmpegArgs = @(
    "-re",
    "-i", $testVideo,
    "-c:v", "libx264",
    "-preset", "ultrafast",
    "-tune", "zerolatency",
    "-f", "rtsp",
    "rtsp://127.0.0.1:8554/main"
)
Start-Process -FilePath $ffmpegExe -ArgumentList $ffmpegArgs
```

## 注意事项

1. 确保测试视频文件存在 (`bin/test.mp4`)
2. FFmpeg 已包含在项目中 (`third_party/ffmpeg/bin/`)
3. 测试完成后按 Enter 键停止所有进程
4. 如果需要持久化运行，可直接启动 mediamtx 实例