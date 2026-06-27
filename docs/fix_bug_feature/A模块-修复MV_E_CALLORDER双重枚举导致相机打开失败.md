# A 模块 — 修复 MV_E_CALLORDER 双重枚举导致相机打开失败

## 变更日期
2026-06-27

## 状态
**✅ 已修复并实机验证通过**

## 问题描述
启动 A 模块（ingest-streaming-module）时，两台海康 GigE 相机均能被 SDK 枚举发现，但 `MV_CC_OpenDevice()` 对两台相机都返回 `-2147483133`（即 `0x80000003` = `MV_E_CALLORDER`："函数调用顺序错误"），导致相机一直处于 `online=false` 状态。

错误日志：
```
[DEBUG] Found GigE camera: serial=F92514845
[DEBUG] Found GigE camera: serial=D91363830
[ERROR] Camera D91363830 failed to open device. Error: -2147483133
[ERROR] Camera F92514845 failed to open device. Error: -2147483133
[Cam cam_01] 0x0 @ 0.0fps | online=false | streaming=false | frames=0
[Cam cam_02] 0x0 @ 0.0fps | online=false | streaming=false | frames=0
```

## 根本原因

在 `CameraDevice::Initialize()` 中存在**双重枚举**模式：

```cpp
MV_CC_DEVICE_INFO_LIST devList = { 0 };

// 第 1 次枚举 — 内部分配 pDeviceInfo 内存
int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devList);

// 第 2 次枚举 — 复用同一个 devList，SDK 内部状态冲突
MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devList);
```

第一次调用后，SDK 为 `devList.pDeviceInfo` 内部分配了内存。第二次调用复用同一个 `devList`（其 `pDeviceInfo` 已非 NULL），导致 SDK 内部传输层状态机异常。这个异常状态传递到后续的 `CreateHandle` → `OpenDevice` 链路，表现为 `MV_E_CALLORDER`。

此外，每个 `CameraDevice::Initialize()` 都独立调用枚举——两台相机在同一个 `IngestEngine::Initialize()` 循环中各调一次，进一步加剧了 SDK 状态紊乱。

## 解决方案

### 核心修复：枚举提升到 IngestEngine 层，全局执行一次

**修改前**（ingest_engine.cpp）：
```cpp
for (const auto& camConfig : config.cameras) {
    auto camera = std::make_unique<CameraDevice>(...);
    camera->Initialize();  // 内部各自枚举（双次）
}
```

**修改后**：
```cpp
// 全局枚举一次
MV_CC_DEVICE_INFO_LIST devList = { 0 };
MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devList);

for (const auto& camConfig : config.cameras) {
    auto camera = std::make_unique<CameraDevice>(...);
    // 按 serial 匹配预枚举的设备信息
    const MV_CC_DEVICE_INFO* matchedInfo = FindBySerial(devList, camConfig.serial);
    camera->Initialize(matchedInfo);  // 传入设备信息，不再内部枚举
}
```

### 多层兜底策略

在 `CameraDevice::Initialize(deviceInfo)` 中增加 3 层 OpenDevice 策略：

| 策略 | 方法 | 说明 |
|------|------|------|
| 1 | 直接 `OpenDevice` | 标准调用（当前生效方案） |
| 2 | Delay 500ms + `OpenDevice` | SDK 内部状态稳定 |
| 3 | `ForceIpEx` + `OpenDevice` | 子网不一致时强制对齐相机 IP |

实际运行中策略 1 直接成功——根因就是双重枚举。

### 增强诊断日志

- 打印每台相机的 IP、子网掩码、Host Adapter IP
- 检测主机与相机是否在同一子网
- 每次 `OpenDevice` 失败时打印策略编号和错误码

## 修改文件清单

| 文件 | 变更 |
|------|------|
| `include/camera_device.h:63` | `Initialize()` 签名改为 `Initialize(const MV_CC_DEVICE_INFO*)` |
| `src/camera_device.cpp:28-124` | 移除双重枚举；接收设备信息参数；3 层 OpenDevice 兜底；增加 `<thread>` `<chrono>` 头文件 |
| `src/ingest_engine.cpp:37-58` | `Initialize()` 中全局枚举一次，按 serial 匹配后传入；打印相机网络配置诊断 |

## 实机验证结果

```json
{
    "status": "running",
    "cam_01_status": "online",
    "cam_02_status": "online",
    "cam_01_fps": 25.0,
    "cam_02_fps": 25.0,
    "online_count": 2,
    "cameras": [
        { "camera_id": "cam_01", "online": true, "streaming": true,
          "width": 1920, "height": 1080, "fps": 25.0, "frame_count": 2493 },
        { "camera_id": "cam_02", "online": true, "streaming": true,
          "width": 1920, "height": 1080, "fps": 25.0, "frame_count": 2492 }
    ]
}
```

### RTSP 流验证
- `rtsp://127.0.0.1:8554/main` → `h264, 1920x1080, 25/1 fps` ✅
- `rtsp://127.0.0.1:8555/aux` → `h264, 1920x1080, 25/1 fps` ✅

### 编译
- **MSBuild Release x64**: 0 warnings, 0 errors ✅
