# 采集与编码分发模块 - 部署指南

## 更新日志
- 2026-06-07: 重写，将 GStreamer DLL 统一放到 `third_party\gstreamer\bin\`，简化部署流程

---

## 一、环境要求

| 软件 | 版本 | 说明 |
|------|------|------|
| Windows | 10/11 x64 | 操作系统 |
| Visual Studio | 2022 v143 | 编译工具 |
| GStreamer | 1.28.3 | ✅ 已包含在 `third_party\` |
| MVS SDK | 5.0+ | ❌ 需要手动复制 |

---

## 二、部署步骤（同学电脑上）

### 步骤 1: 拉取代码

```bash
git clone <仓库地址>
cd football-auto-broadcast-
```

### 步骤 2: 复制 MVS SDK DLL（只需一次）

MVS SDK 是海康威视工业相机的 SDK，需要手动获取：

1. 从 MVS 客户端安装目录找到 `win64\` 文件夹
2. 复制整个 `win64\` 文件夹到项目目录：

```
third_party\mvs_sdk\win64\
```

必须包含的文件：
- `MvCameraControl.dll`
- `MvCameraControl.lib`

> 📌 **提示**: MVS SDK 可从海康威视官网下载 "MVS 机器视觉软件"

### 步骤 3: 编译项目

1. 用 VS 打开 `services\ingest-streaming-module\ingest-streaming-module.slnx`
2. 选择 **Release + x64**
3. 按 `Ctrl+Shift+B` 编译

### 步骤 4: 部署依赖文件

#### 方式 A: 使用部署脚本（推荐）

```batch
cd services\ingest-streaming-module
deploy.bat
```

#### 方式 B: 手动复制

在 `x64\Release\` 目录执行：

```batch
# 复制 GStreamer 运行时 DLL
xcopy ..\..\..\third_party\gstreamer\bin\*.dll . /Y /Q

# 复制 GStreamer 插件
if not exist "lib\gstreamer-1.0\" mkdir lib\gstreamer-1.0
xcopy ..\..\..\third_party\gstreamer\lib\gstreamer-1.0\*.dll lib\gstreamer-1.0\ /Y /Q

# 复制配置文件
xcopy ..\bin\*.yml . /Y /Q
xcopy ..\bin\*.exe . /Y /Q
```

### 步骤 5: 运行

```batch
cd x64\Release

# 启动 MediaMTX（RTSP 服务器）
start mediamtx.exe mediamtx_8554.yml

# 运行主程序
ingest-streaming-module.exe
```

---

## 三、目录结构

部署后 `x64\Release\` 目录应包含：

```
x64\Release\
├── ingest-streaming-module.exe      # 主程序
├── gstreamer-1.0-0.dll             # GStreamer 核心
├── glib-2.0-0.dll                  # GLib 核心
├── gstapp-1.0-0.dll                # appsrc 插件
├── gstbase-1.0-0.dll               # 基础插件
├── x264-164.dll                    # x264 编码器
├── MvCameraControl.dll             # MVS SDK
├── lib\
│   └── gstreamer-1.0\
│       ├── gstapp.dll              # 插件
│       ├── gstx264.dll            # 插件
│       └── ... (更多插件)
├── mediamtx.exe                    # RTSP 服务器
├── mediamtx_8554.yml              # MediaMTX 配置
└── ...
```

---

## 四、验证部署

### 检查 DLL 是否完整

运行程序时如果提示缺少 DLL，按以下方式检查：

```batch
# 检查 GStreamer 核心 DLL
dir x64\Release\gstreamer-1.0-0.dll

# 检查插件目录
dir x64\Release\lib\gstreamer-1.0\gstapp.dll
```

### 测试 RTSP 流

使用 VLC 或 ffplay 测试：

```batch
ffplay rtsp://127.0.0.1:8554/main
ffplay rtsp://127.0.0.1:8555/aux
```

---

## 五、常见问题

### Q1: 提示找不到 `gstreamer-1.0-0.dll`

**原因**: GStreamer DLL 未复制到输出目录

**解决**: 
```batch
xcopy third_party\gstreamer\bin\*.dll x64\Release\ /Y /Q
```

### Q2: 提示 `no element "appsrc"`

**原因**: GStreamer 插件未正确放置

**解决**:
```batch
# 确保插件目录存在
if not exist "x64\Release\lib\gstreamer-1.0\" mkdir x64\Release\lib\gstreamer-1.0
xcopy third_party\gstreamer\lib\gstreamer-1.0\*.dll x64\Release\lib\gstreamer-1.0\ /Y /Q
```

### Q3: 提示找不到 `MvCameraControl.dll`

**原因**: 未复制 MVS SDK DLL

**解决**: 从 MVS SDK 安装目录复制 `win64\` 到 `third_party\mvs_sdk\`

### Q4: MediaMTX 无法启动（端口被占用）

**解决**: 关闭占用 8554 端口的程序，或修改 `mediamtx_8554.yml` 中的端口

---

## 六、环境变量（可选）

如果程序仍无法找到插件，可设置：

```batch
set GST_PLUGIN_PATH=x64\Release\lib\gstreamer-1.0
set PATH=%PATH%;x64\Release
```

---

## 七、快速检查清单

- [ ] MVS SDK DLL 已复制到 `third_party\mvs_sdk\win64\`
- [ ] 项目已编译（Release + x64）
- [ ] `deploy.bat` 已执行
- [ ] `mediamtx.exe` 和配置文件已复制
- [ ] 运行程序无 DLL 缺失错误
