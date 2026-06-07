# 采集与编码分发模块 - 部署指南

## 更新日志
- 2026-06-07: 重写，所有依赖 DLL 已包含在项目中，部署更简单

---

## 一、环境要求

| 软件 | 版本 | 说明 |
|------|------|------|
| Windows | 10/11 x64 | 操作系统 |
| Visual Studio | 2022 v143 | 编译工具 |
| GStreamer | 1.28.3 | ✅ 已包含在 `third_party\gstreamer\bin\` |
| MVS SDK | 5.0+ | ✅ 已包含在 `third_party\mvs_sdk\win64\` |

**所有依赖 DLL 都已随仓库一起提供，无需手动下载！**

---

## 二、部署步骤（同学电脑上）

### 步骤 1: 拉取代码

```bash
git clone <仓库地址>
cd football-auto-broadcast-
```

### 步骤 2: 编译项目

1. 用 VS 打开 `services\ingest-streaming-module\ingest-streaming-module.slnx`
2. 选择 **Release + x64**
3. 按 `Ctrl+Shift+B` 编译

### 步骤 3: 运行部署脚本（自动复制依赖）

```batch
cd services\ingest-streaming-module
deploy.bat
```

脚本会自动复制：
- GStreamer 运行时 DLL（146个）
- GStreamer 插件
- MVS SDK DLL（52个）
- 配置文件

### 步骤 4: 运行

```batch
cd x64\Release

# 启动 MediaMTX（RTSP 服务器）
start mediamtx.exe mediamtx_8554.yml

# 运行主程序
ingest-streaming-module.exe
```

---

## 三、目录结构

### 依赖文件位置

```
third_party\
├── gstreamer\
│   ├── bin\                    # 146 个运行时 DLL
│   └── lib\gstreamer-1.0\      # 插件 DLL
└── mvs_sdk\
    └── win64\                  # 52 个 SDK DLL
```

### 输出目录结构

```
x64\Release\
├── ingest-streaming-module.exe  # 主程序
├── gstreamer-1.0-0.dll        # GStreamer 核心
├── MvCameraControl.dll         # MVS SDK
├── lib\gstreamer-1.0\          # 插件
├── mediamtx.exe                # RTSP 服务器
└── mediamtx_8554.yml          # MediaMTX 配置
```

---

## 四、验证部署

### 测试 RTSP 流

```batch
ffplay rtsp://127.0.0.1:8554/main
ffplay rtsp://127.0.0.1:8555/aux
```

### HTTP 状态接口

```batch
curl http://127.0.0.1:8081/api/v1/ingest/status
```

---

## 五、常见问题

### Q: 提示找不到 DLL

**解决**: 确保运行了 `deploy.bat`

### Q: MediaMTX 无法启动（端口被占用）

**解决**: 关闭占用 8554 端口的程序，或修改 `mediamtx_8554.yml`

---

## 六、快速检查清单

- [ ] 代码已拉取
- [ ] 项目已编译（Release + x64）
- [ ] `deploy.bat` 已执行
- [ ] 运行程序无 DLL 缺失错误
