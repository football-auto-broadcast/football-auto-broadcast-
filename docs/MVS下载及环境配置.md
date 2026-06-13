# MVS 5.0 采集模块环境搭建说明 v1.1
**文档版本**：v1.1
**适用版本**：MVS_STD_5.0.0_260417
**适用模块**：ingest-streaming-module（A模块）
**适用IDE**：Visual Studio 2022
**更新日期**：2026-06-13

---

## 1. 安装包信息
| 项 | 值 |
| --- | --- |
| 安装包名称 | MVS_STD_5.0.0_260417.exe |
| 安装包大小 | 467MB |
| 本地安装路径 | `D:\C++ Big project\MVS软件\MVS` |
| 系统运行库路径 | `C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64`（安装时自动添加） |

---

## 2. 安装步骤
1. 运行MVS安装包，默认下一步
2. 勾选安装选项：
   - ✅ 完整安装（包含SDK、驱动、虚拟相机）
   - ✅ 添加环境变量
   - ✅ 安装GigE相机驱动
3. 安装完成后重启电脑
4. 验证环境变量：系统Path中存在`C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64`即为成功

---

## 3. VS工程配置

> **重要**：本项目已将 MVS SDK 打包到 `third_party/mvs_sdk/` 目录下。**直接打开 .vcxproj 即可编译**，SDK 路径已在项目文件中写死，无需手动配置。

### 3.1 平台与配置
- 平台：`x64`（海康SDK仅支持64位，禁止使用Win32）
- 配置：`Release`（Debug模式仅用于调试，最终运行使用Release）

### 3.2 VC++目录配置（已在 .vcxproj 中写死）
| 项 | 路径 |
| --- | --- |
| 包含目录 | `$(ProjectDir)include;$(ProjectDir)third_party\mvs_sdk\Includes` |
| 库目录 | `$(ProjectDir)third_party\mvs_sdk\win64` |

### 3.3 链接器配置（已在 .vcxproj 中写死）
- 附加依赖项：`MvCameraControl.lib`

### 3.4 编译选项
- C/C++ → 常规 → SDL检查：设置为`否(/sdl-)`

### 3.5 编译操作
1. 打开 `services/ingest-streaming-module/ingest-streaming-module.vcxproj`
2. 选择 **Release + x64**
3. 按 `Ctrl+Shift+B` 编译
4. 编译输出到 `x64/Release/ingest_streaming_service.exe`

---

## 4. MVS 5.0 代码适配说明
MVS 5.0与旧版SDK结构体存在差异，枚举相机时需注意：
1. 旧版SDK：IP为字符串成员`chCurrentIp`
2. MVS 5.0 SDK：IP为大端整数成员`nCurrentIp`，需转换为点分十进制：
```cpp
// MVS 5.0 专属IP转换代码（相机设备类中已实现）
std::cout << "IP地址: "
    << ((dev->SpecialInfo.stGigEInfo.nCurrentIp >> 24) & 0xFF) << "."
    << ((dev->SpecialInfo.stGigEInfo.nCurrentIp >> 16) & 0xFF) << "."
    << ((dev->SpecialInfo.stGigEInfo.nCurrentIp >> 8) & 0xFF) << "."
    << (dev->SpecialInfo.stGigEInfo.nCurrentIp & 0xFF) << std::endl;
```

---

## 5. 已知问题

| 问题 | 原因 | 解决方法 |
|------|------|----------|
| 编译报错 LNK2019（找不到 MVS 函数） | third_party/mvs_sdk 目录不存在或路径错误 | 确认 third_party/mvs_sdk/Includes 和 win64/ 目录存在 |
| GigE 相机枚举不到 | 防火墙或网线问题 | 检查相机 IP 与主机 IP 是否在同一网段，检查 MVS 驱动是否安装 |
| 相机序列号与 MVS 客户端不一致 | 枚举方式不同 | 以相机外壳标签和 MVS 客户端显示为准 |