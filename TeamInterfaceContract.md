## **足球赛事自动转播系统团队接口约定文档（Windows 原生版）**
## **目录**
    1. 文档信息
    2. 文档目的
    3. 架构总览
    4. 运行环境冻结要求
    5. 模块定义与责任边界
    6. 进程、端口与本机地址约定
    7. Windows 文件系统与目录契约
    8. 通信总原则
    9. 数据格式总约定
    10. 模块间接口契约
    11. 状态机与错误码约定
    12. 日志与追踪约定
    13. 联调顺序约定
    14. 变更控制规则
    15. 责任归属规则
    16. 可行性校验
    17. 冻结结论

---

## **1. 文档信息**
**文档名称**：Team Interface Contract for Windows  
**版本**：v1.0  
**适用阶段**：MVP 第一版  
**适用系统**：Windows 原生运行  
**适用项目**：足球赛事自动转播系统  
**冻结要求**：第 1 周结束前冻结并执行  
**适用团队**：5 位工程师并行开发与联调

---

## **2. 文档目的**
本文件用于冻结 5 个模块在 Windows 单主机集中式架构下的：

1. 模块边界
2. 输入输出
3. 本机通信方式
4. 接口路径
5. 文件路径
6. 状态机
7. 错误码
8. 联调顺序
9. 责任归属

本文件冻结后，默认作为开发契约执行。除非通过团队评审，不允许单个模块自行修改接口语义、字段含义、路径含义和状态机定义。

---

## **3. 架构总览**
### **3.1 硬件架构**
当前硬件架构固定为：

+ 工业相机 1，GigE，主机位
+ 工业相机 2，GigE，辅机位
+ 千兆 PoE 交换机
+ Windows 主机，GPU 为 RTX 4080
+ 可选控制端，通过浏览器访问后台

GigE Vision 体系天然适合多相机系统，支持 1 Gbit/s 带宽、长线缆和 PoE 单线供电，这与当前硬件方案匹配。 

### **3.2 软件架构**
系统采用**单主机集中式处理**。5 个模块统一运行在同一台 Windows 主机上：

| **模块编号** | **模块名称** | **英文名** | **负责人** |
| --- | --- | --- | --- |
| A | 采集与编码分发模块 | ingest-streaming-module | 工程师 A |
| B | 录像与主画面生成模块 | record-program-module | 工程师 B |
| C | 视觉分析模块 | vision-event-module | 工程师 C |
| D | 事件与集锦生成模块 | highlight-generation-module | 工程师 D |
| E | 平台与调度模块 | platform-orchestration-module | 工程师 E |




### **3.3 架构原则**
当前架构中，工业相机由 A 模块通过厂商 SDK 直接取流；B、C、D、E 均通过本机通信与本地文件系统协作。HIKROBOT 官方下载中心提供 Windows 工业相机 SDK 运行包，这使 Windows 原生接入工业相机成为正式方案。

## **4. 运行环境冻结要求**
### **4.1 操作系统**
统一要求：Windows 10 64 位或 Windows 11 64 位。  
视觉推理与相机 SDK、OpenCV、GStreamer、ONNX Runtime 均以 Windows 官方支持链路为准。 

### **4.2 工具链**
统一要求：

+ Visual Studio 2022
+ CMake
+ Git
+ HIKROBOT Windows SDK
+ GStreamer for Windows
+ OpenCV for Windows
+ ONNX Runtime for Windows

OpenCV 官方 Windows 安装文档要求具备有效编译器与 CMake；GStreamer 官方有独立的 Windows 安装与部署文档；ONNX Runtime 官方提供 Windows 路线，并推荐 Windows 开发使用 WinML 能力。 

### **4.3 部署方式**
本契约冻结版本采用 **Windows 原生进程运行**。  
当前版本不将 Docker、WSL2、Linux 容器纳入正式交付路径。

---

## **5. 模块定义与责任边界**
## **5.1 模块 A：ingest-streaming-module**
### **负责**
1. 工业相机 SDK 初始化
2. 两台 GigE 工业相机枚举与绑定
3. 工业相机参数加载
4. 连续取流
5. 基础编码压缩
6. 向 B、C 提供标准化视频输入
7. 流状态监控与异常上报

### **不负责**
1. 原始录像落盘
2. 主画面裁切
3. 候选事件识别
4. 集锦生成
5. 后台页面

### **强制要求**
A 模块必须通过厂商 SDK 取流，不允许把工业相机等同于普通 RTSP IPC。这个要求由当前硬件类型决定。HIKROBOT 官方提供工业相机 SDK 与客户端软件用于设备连接、参数设置与图像采集。 

---

## **5.2 模块 B：record-program-module**
### **负责**
1. 两路原始录像保存
2. 主画面生成
3. 主画面裁切和平滑
4. 主画面录像保存
5. 主画面预览输出
6. 文件归档与索引

### **不负责**
1. 工业相机 SDK 取流
2. 事件识别
3. 集锦拼接
4. 平台页面

---

## **5.3 模块 C：vision-event-module**
### **负责**
1. 足球基础检测
2. 高活跃区域分析
3. 主画面关注区域输出
4. 进球候选识别
5. 精彩片段候选识别
6. 候选事件结构化输出

### **不负责**
1. 球员号码识别
2. 个人集锦支持
3. 录像文件保存
4. 主画面裁切实现
5. 集锦导出

### **强制要求**
C 模块只输出事件候选和关注区域，不输出球员身份、号码、个人片段逻辑。

---

## **5.4 模块 D：highlight-generation-module**
### **负责**
1. 读取录像文件
2. 读取候选事件
3. 裁切事件视频片段
4. 生成全场精彩集锦
5. 导出集锦文件
6. 返回任务状态与结果

### **不负责**
1. 相机接入
2. 视频裁切预览
3. 视觉事件识别
4. 后台页面

---

## **5.5 模块 E：platform-orchestration-module**
### **负责**
1. 创建比赛
2. 开始比赛
3. 结束比赛
4. 调度 A、B、C、D
5. 汇总状态
6. 主画面预览页面
7. 集锦结果展示与下载
8. 配置管理

### **不负责**
1. 相机 SDK 接入
2. 视频编码实现
3. 视觉推理
4. 集锦拼接实现

---

## **6. 进程、端口与本机地址约定**
### **6.1 进程名冻结**
| **模块** | **Windows 可执行文件名** |
| --- | --- |
| A | ingest_streaming_service.exe |
| B | record_program_service.exe |
| C | vision_event_service.exe |
| D | highlight_generation_service.exe |
| E | platform_orchestration_service.exe |


### **6.2 HTTP 端口冻结**
| **模块** | **默认端口** |
| --- | --- |
| E | 8080 |
| A | 8081 |
| B | 8082 |
| C | 8083 |
| D | 8084 |


### **6.3 本机 RTSP 端口冻结**
| **用途** | **默认端口** |
| --- | --- |
| 主机位内部流 | 8554 |
| 辅机位内部流 | 8555 |
| 主画面预览流 | 8560 |


### **6.4 地址冻结**
所有模块在主机内部统一通过 `127.0.0.1` 访问。

禁止：

+ 在本机模块间使用公网 IP
+ 在代码中写死机器名
+ 为了“方便调试”私自改端口

## **7. Windows 文件系统与目录契约**
### **7.1 数据根目录冻结**
统一抽象变量名：

```plain
{data_root}
```

Windows 正式部署推荐值：

```plain
D:\football\data
```

### **7.2 最低目录结构冻结**
```plain
{data_root}\raw
{data_root}\program
{data_root}\output
{data_root}\metadata
{data_root}\logs
{data_root}\temp
```

### 7.3 文件命名冻结
#### 原始录像
```plain
{data_root}\raw\{match_id}\cam_01.mp4
{data_root}\raw\{match_id}\cam_02.mp4
```

#### 主画面录像
```plain
{data_root}\program\{match_id}\program.mp4
```

#### 候选事件文件
```plain
{data_root}\metadata\{match_id}\event_candidates.json
```

#### 集锦输出
```plain
{data_root}\output\{match_id}\full_highlight.mp4
```

### 7.4 强制要求
1.  所有路径必须从配置文件读取 
2.  禁止在代码中直接写 `D:\football\data`
3.  禁止模块自己创建第二套根目录 
4.  写文件后，必须在文件句柄关闭后再更新索引或返回结果



## 8. 通信总原则
### 8.1 控制通信
所有控制类接口统一使用：

+  HTTP REST 
+  JSON 
+  本机地址 `127.0.0.1`

### 8.2 视频通信
A 向 B、C 提供视频输入。  
MVP 第一版统一使用 **本机 RTSP** 作为模块间视频输入协议。GStreamer 官方支持 Windows 安装与部署，因此这条链路在 Windows 上可执行。

### 8.3 文件通信
B、C、D、E 之间通过本地文件系统共享录像、元数据和集锦输出。

### 8.4 禁止事项
禁止：

1.  用共享文件代替控制接口 
2.  用 JSON 传输视频内容 
3.  在代码中写死路径、端口、IP 
4.  绕开 E 模块自行定义任务调度协议 

---

## 9. 数据格式总约定
### 9.1 JSON 字段命名冻结
统一使用 `snake_case`。

合法示例：

+ `match_id`
+ `camera_id`
+ `stream_uri`
+ `event_type`
+ `timestamp_ms`

非法示例：

+ `matchId`
+ `CameraID`
+ `event-type`

### 9.2 时间字段冻结
#### 绝对时间
+ `timestamp_ms`
+  类型：integer 
+  单位：毫秒 

#### 相对比赛时间
+ `start_sec`
+ `end_sec`
+  类型：number 
+  单位：秒 

### 9.3 通用响应格式冻结
所有 HTTP 接口统一返回：

```plain
{
  "code": 0,
  "message": "ok",
  "data": {}
}
```

### 9.4 错误码冻结
| code | 含义 |
| --- | --- |
| `0` | 成功 |
| `1001` | 参数错误 |
| `1002` | 资源未初始化 |
| `1003` | 输入源不可用 |
| `1004` | 视频流异常 |
| `1005` | 文件写入失败 |
| `1006` | 文件读取失败 |
| `1007` | 任务执行失败 |
| `1008` | 上游服务不可达 |
| `1009` | 资源不存在 |
| `1010` | 状态冲突 |
| `1011` | 配置错误 |
| `1012` | 工业相机 SDK 初始化失败 |
| `1013` | 工业相机枚举失败 |
| `1014` | 相机取流失败 |


### 9.5 状态值冻结
统一使用：

+ `idle`
+ `initializing`
+ `running`
+ `recording`
+ `processing`
+ `success`
+ `failed`
+ `stopped`

---

## 10. 模块间接口契约
## 10.1 模块 E -> 模块 A
### 接口 1：初始化采集任务
**方法**：`POST`  
**路径**：

```plain
/api/v1/ingest/matches/init
```

**请求体**：

```plain
{
  "match_id": "match_20260405_001",
  "match_name": "school_match_demo",
  "camera_configs": [
    {
      "camera_id": "cam_01",
      "role": "main",
      "sdk_index": 0
    },
    {
      "camera_id": "cam_02",
      "role": "aux",
      "sdk_index": 1
    }
  ],
  "capture_config": {
    "resolution": "1920x1080",
    "fps": 25,
    "codec": "h265"
  }
}
```

### 接口 2：开始采集与分发
**方法**：`POST`  
**路径**：

```plain
/api/v1/ingest/matches/{match_id}/start
```

### 接口 3：停止采集与分发
**方法**：`POST`  
**路径**：

```plain
/api/v1/ingest/matches/{match_id}/stop
```

### 接口 4：查询采集状态
**方法**：`GET`  
**路径**：

```plain
/api/v1/ingest/matches/{match_id}/status
```

**响应 data 示例**：

```plain
{
  "match_id": "match_20260405_001",
  "status": "running",
  "streams": [
    {
      "camera_id": "cam_01",
      "role": "main",
      "stream_uri": "rtsp://127.0.0.1:8554/main",
      "fps": 25,
      "status": "online"
    },
    {
      "camera_id": "cam_02",
      "role": "aux",
      "stream_uri": "rtsp://127.0.0.1:8555/aux",
      "fps": 25,
      "status": "online"
    }
  ]
}
```

---

## 10.2 模块 E -> 模块 B
### 接口 1：初始化录像与主画面任务
**方法**：`POST`  
**路径**：

```plain
/api/v1/record/matches/init
```

**请求体**：

```plain
{
  "match_id": "match_20260405_001",
  "input_streams": [
    {
      "camera_id": "cam_01",
      "role": "main",
      "stream_uri": "rtsp://127.0.0.1:8554/main"
    },
    {
      "camera_id": "cam_02",
      "role": "aux",
      "stream_uri": "rtsp://127.0.0.1:8555/aux"
    }
  ],
  "storage_config": {
    "raw_root": "{data_root}\\raw",
    "program_root": "{data_root}\\program"
  },
  "program_config": {
    "output_resolution": "1920x1080",
    "default_mode": "follow_focus_region"
  }
}
```

### 接口 2：开始录制与主画面输出
**方法**：`POST`  
**路径**：

```plain
/api/v1/record/matches/{match_id}/start
```

### 接口 3：停止录制与主画面输出
**方法**：`POST`  
**路径**：

```plain
/api/v1/record/matches/{match_id}/stop
```

### 接口 4：查询录像状态
**方法**：`GET`  
**路径**：

```plain
/api/v1/record/matches/{match_id}/status
```

### 接口 5：查询文件索引
**方法**：`GET`  
**路径**：

```plain
/api/v1/record/matches/{match_id}/files
```

---

## 10.3 模块 C -> 模块 B
### 接口：更新主画面关注区域
**方法**：`POST`  
**路径**：

```plain
/api/v1/record/matches/{match_id}/focus-region
```

**请求体**：

```plain
{
  "match_id": "match_20260405_001",
  "camera_id": "cam_01",
  "timestamp_ms": 1712323200123,
  "focus_region": {
    "x": 1200,
    "y": 650,
    "width": 1400,
    "height": 800
  },
  "source_type": "motion_cluster",
  "confidence": 0.87
}
```

### 冻结规则
+ `camera_id` 必须是 `cam_01`
+ `confidence` 必须在 `[0,1]`
+ `width` 和 `height` 必须大于 0 
+  B 模块必须支持默认中心区域回退 

---

## 10.4 模块 E -> 模块 C
### 接口 1：初始化视觉分析任务
**方法**：`POST`  
**路径**：

```plain
/api/v1/vision/matches/init
```

**请求体**：

```plain
{
  "match_id": "match_20260405_001",
  "streams": [
    {
      "camera_id": "cam_01",
      "role": "main",
      "stream_uri": "rtsp://127.0.0.1:8554/main"
    },
    {
      "camera_id": "cam_02",
      "role": "aux",
      "stream_uri": "rtsp://127.0.0.1:8555/aux"
    }
  ],
  "event_config": {
    "enable_goal_candidate": true,
    "enable_shot_candidate": true,
    "enable_danger_attack_candidate": true,
    "enable_celebration_candidate": true
  }
}
```

### 接口 2：开始视觉分析
**方法**：`POST`  
**路径**：

```plain
/api/v1/vision/matches/{match_id}/start
```

### 接口 3：停止视觉分析
**方法**：`POST`  
**路径**：

```plain
/api/v1/vision/matches/{match_id}/stop
```

### 接口 4：查询视觉状态
**方法**：`GET`  
**路径**：

```plain
/api/v1/vision/matches/{match_id}/status
```

---

## 10.5 模块 C -> 模块 D
### 接口：候选事件查询接口
**方法**：`GET`  
**路径**：

```plain
/api/v1/vision/matches/{match_id}/event-candidates
```

**查询参数**：

+ `start_ms`
+ `end_ms`

**响应 data 示例**：

```plain
{
  "match_id": "match_20260405_001",
  "events": [
    {
      "event_id": "evt_0001",
      "event_type": "goal_candidate",
      "start_sec": 312.4,
      "end_sec": 320.6,
      "confidence": 0.92,
      "camera_id": "cam_01"
    }
  ]
}
```

### 事件类型冻结
+ `goal_candidate`
+ `shot_candidate`
+ `danger_attack_candidate`
+ `celebration_candidate`

### 本地事件备份文件
```plain
{data_root}\metadata\{match_id}\event_candidates.json
```

---

## 10.6 模块 E -> 模块 D
### 接口 1：初始化集锦任务上下文
**方法**：`POST`  
**路径**：

```plain
/api/v1/highlight/matches/init
```

**请求体**：

```plain
{
  "match_id": "match_20260405_001",
  "video_files": {
    "program_file": "{data_root}\\program\\match_20260405_001\\program.mp4",
    "raw_files": [
      "{data_root}\\raw\\match_20260405_001\\cam_01.mp4",
      "{data_root}\\raw\\match_20260405_001\\cam_02.mp4"
    ]
  },
  "metadata_source": {
    "type": "http",
    "event_api": "http://127.0.0.1:8083/api/v1/vision/matches/match_20260405_001/event-candidates"
  }
}
```

### 接口 2：创建全场精彩集锦任务
**方法**：`POST`  
**路径**：

```plain
/api/v1/highlight/matches/{match_id}/full-highlight
```

**请求体**：

```plain
{
  "event_types": [
    "goal_candidate",
    "shot_candidate",
    "danger_attack_candidate",
    "celebration_candidate"
  ],
  "pre_roll_sec": 5,
  "post_roll_sec": 5,
  "max_duration_sec": 180
}
```

### 接口 3：查询集锦任务状态
**方法**：`GET`  
**路径**：

```plain
/api/v1/highlight/tasks/{task_id}
```

---

## 11. 状态机与错误码约定
### 11.1 模块状态流转
#### A 模块
`idle -> initializing -> running -> stopped / failed`

#### B 模块
`idle -> initializing -> recording -> stopped / failed`

#### C 模块
`idle -> initializing -> running -> stopped / failed`

#### D 模块
`idle -> processing -> success / failed`

#### E 模块
`idle -> running`

### 11.2 强制要求
+  状态流转必须单向清晰 
+  不允许出现未定义状态 
+  不允许成功和失败同时为真 
+  状态变化必须写日志 

---

## 12. 日志与追踪约定
### 12.1 最低日志字段冻结
每条日志至少包含：

+ `timestamp`
+ `module_name`
+ `level`
+ `match_id`
+ `task_id`
+ `request_id`
+ `message`

### 12.2 日志级别冻结
+ `DEBUG`
+ `INFO`
+ `WARN`
+ `ERROR`

### 12.3 请求头推荐
所有模块间 HTTP 请求建议带：

```plain
Content-Type: application/json
Accept: application/json
X-Request-Id: req_20260405_0001
X-Match-Id: match_20260405_001
X-Caller-Module: platform-orchestration-module
```

---

## 13. 联调顺序约定
### 第一步：A 模块单独联通
目标：

+  两台工业相机可被识别 
+  SDK 初始化成功 
+  两路视频可持续获取 
+  主机位和辅机位内部流可播放 

### 第二步：A + B 联调
目标：

+  两路原始录像可保存 
+  主画面可生成 
+  主画面预览可查看 

### 第三步：A + C 联调
目标：

+  视觉模块可消费两路视频输入 
+  可输出关注区域 
+  可输出第一版候选事件 

### 第四步：B + C 联调
目标：

+  主画面能接收关注区域进行裁切 
+  主画面裁切具备基本平滑效果 

### 第五步：C + D 联调
目标：

+  D 能读取候选事件 
+  D 能读取录像文件 
+  D 能生成第一版全场精彩集锦 

### 第六步：E 接入所有模块
目标：

+  后台可创建比赛 
+  后台可开始比赛与结束比赛 
+  后台可查看主画面预览 
+  后台可触发集锦生成 
+  后台可下载集锦结果 

---

## 14. 变更控制规则
### 14.1 冻结规则
+  第 1 周结束前冻结 `v1`
+  第 2 周到第 6 周不允许修改字段语义 
+  第 4 周后禁止新增跨模块关键字段 
+  第 6 周后只允许修 bug，不允许大改接口 

### 14.2 允许的变更
允许：

1.  新增可选字段 
2.  补充日志字段 
3.  修复拼写错误 
4.  新增不影响旧逻辑的状态信息 

### 14.3 不允许的变更
不允许：

1.  修改已有字段名称 
2.  改变已有字段含义 
3.  改变路径语义 
4.  改动事件类型枚举 
5.  改变端口与地址冻结值而不经评审 

---

## 15. 责任归属规则
### 15.1 A 模块责任边界
工业相机枚举失败、SDK 初始化失败、取流失败、内部视频输入不可用，责任优先归 A。

### 15.2 B 模块责任边界
录像文件缺失、主画面不生成、裁切异常、主画面文件损坏，责任优先归 B。

### 15.3 C 模块责任边界
关注区域不输出、候选事件为空、事件数据格式错误，责任优先归 C。

### 15.4 D 模块责任边界
有录像和事件输入但集锦不生成、输出文件损坏，责任优先归 D。

### 15.5 E 模块责任边界
后台无法调度、状态不显示、任务无法触发、下载入口无效，责任优先归 E。

---

## 16. 可行性校验
### 16.1 Windows 工具链可行性
可行。  
HIKROBOT 官方提供 Windows 工业相机 SDK；GStreamer 官方提供 Windows 安装文档；OpenCV 官方提供 Windows 安装与构建路径；ONNX Runtime 官方提供 Windows 运行路线并推荐 WinML 作为 Windows 开发路径。

### 16.2 单主机集中式可行性
可行。  
RTX 4080 主机在算力上足以承担双路采集、录像、主画面生成、视觉分析和赛后集锦生成。这里的契约目标本身也已经收缩到 MVP 所需的自动转播主画面、进球与精彩片段识别、全场精彩集锦，不包含号码识别和个人集锦。

### 16.3 风险点
主要风险如下：

1.  A 模块工业相机 SDK 接入复杂度高 
2.  双路视频写盘压力需要尽早压测 
3.  广角画面下球检测稳定性有限 
4.  单主机多进程资源竞争需要提前验证 

### 16.4 风险规避要求
1.  第 1 周优先打通工业相机 SDK 取流 
2.  第 2 周前完成双流写盘压力测试 
3.  第 3 周前固定相机参数与镜头安装方式 
4.  第 4 周前完成主画面与关注区域联动 



