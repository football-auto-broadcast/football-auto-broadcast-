# MVS 5.0 采集模块环境搭建说明 v1
**文档版本**：v1.0  
**适用版本**：MVS_STD_5.0.0_260417  
**适用模块**：ingest-streaming-module（A模块）  
**适用IDE**：Visual Studio 2026  
**更新日期**：2026-05-24

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

## 3. VS工程配置（必做）
### 3.1 平台与配置
- 平台：`x64`（海康SDK仅支持64位，禁止使用Win32）
- 配置：`Release`（Debug模式仅用于调试，最终运行使用Release）

### 3.2 VC++目录配置
| 项 | 路径 |
| --- | --- |
| 包含目录 | `D:\C++ Big project\MVS软件\MVS\Development\Includes` |
| 库目录 | `D:\C++ Big project\MVS软件\MVS\Development\Libraries\win64` |

### 3.3 链接器配置
- 附加依赖项：添加`MvCameraControl.lib`

### 3.4 编译选项
- C/C++ → 常规 → SDL检查：设置为`否(/sdl-)`

---

## 4. MVS 5.0 代码适配说明
MVS 5.0与旧版SDK结构体存在差异，枚举相机时需注意：
1. 旧版SDK：IP为字符串成员`chCurrentIp`
2. MVS 5.0 SDK：IP为大端整数成员`nCurrentIp`，需转换为点分十进制：
```cpp
// MVS 5.0 专属IP转换代码
std::cout << "IP地址: " 
    << ((dev->SpecialInfo.stGigEInfo.nCurrentIp >> 24) & 0xFF) << "." 
    << ((dev->SpecialInfo.stGigEInfo.nCurrentIp >> 16) & 0xFF) << "." 
    << ((dev->SpecialInfo.stGigEInfo.nCurrentIp >> 8) & 0xFF) << "." 
    << (dev->SpecialInfo.stGigEInfo.nCurrentIp & 0xFF) << std::endl;