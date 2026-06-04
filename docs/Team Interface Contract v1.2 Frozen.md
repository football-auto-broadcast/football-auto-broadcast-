# 足球赛事自动转播系统团队接口约定文档（Windows 原生冻结版）
## 文档信息
**文档名称**：Team Interface Contract for Windows Frozen  
**版本**：v1.2 Frozen  
**适用阶段**：MVP 第一版冻结执行  
**适用系统**：Windows 原生运行  
**适用架构**：双机位可裁切、单主机集中式处理、海康 GigE 工业相机接入  
**冻结要求**：本版本评审通过后执行；除非团队评审通过，不允许单模块私自改动接口语义、字段含义、路径含义、时间基准、状态机与错误码  
**适用团队**：5 位工程师并行开发与联调

---

## 1. 文档目的
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
10. 时间基准
11. 视频输入输出规格
12. 双机位裁切、多机位决策与赛后集锦的协作方式

---

## 2. 架构总览
<!-- 这是一个文本绘图，源码为：flowchart TB

    %% =========================
    %% Hardware Layer
    %% =========================
    subgraph HW["硬件层 Hardware Layer"]
        CAM1["cam_01
海康 MV-CE050-30GC
6mm C接口镜头
主机位
安装位置：半场边线中路附近
职责：半场全局覆盖 / 默认主画面来源"]

        CAM2["cam_02
海康 MV-CE050-30GC
6mm C接口镜头
辅机位
安装位置：底线角区附近
职责：门前区域 / 边路推进 / 局部精彩补充"]

        SW["千兆 PoE 交换机
接入：
cam_01 + cam_02 + 主机"]

        HOST["Windows 主机
RTX 4080
单主机集中式处理
数据根目录：
D:\\football\\data"]

        VIEW["可选控制端
浏览器访问后台
查看状态 / 主画面 / 集锦结果"]
    end

    CAM1 -->|"GigE / 静态IP"| SW
    CAM2 -->|"GigE / 静态IP"| SW
    SW -->|"千兆链路"| HOST
    VIEW -->|"HTTP 8080"| HOST

    %% =========================
    %% Host Internal Modules
    %% =========================
    subgraph PLATFORM["主机内部模块层 Application Layer"]
        direction LR

        subgraph A["A 采集与编码分发模块
ingest-streaming-module
HTTP: 8081"]
            A1["职责
1. 海康 MVS SDK 初始化
2. 相机绑定与参数加载
3. 连续取流
4. H.264 编码
5. 向 B/C 输出标准化 RTSP"]

            A2["标准化输出
cam_01:
rtsp://127.0.0.1:8554/main
cam_02:
rtsp://127.0.0.1:8555/aux

冻结规格：
1920x1080 @ 25fps
H.264"]
        end

        subgraph B["B 录像与主画面生成模块
record-program-module
HTTP: 8082"]
            B1["职责
1. 两路原始录像保存
2. cam_01 裁切画面生成
3. cam_02 裁切画面生成
4. 主画面选择与输出
5. 主画面录像保存
6. record_index.json 输出"]

            B2["策略
主画面输出：
1920x1080 @ 25fps

裁切输出：
统一 16:9
先扩边，再贴边裁剪
轻微平滑，偏防抖
最小切镜保持 2 秒
program_decision 超时回退 cam_01"]

            B3["RTSP 预览输出
rtsp://127.0.0.1:8560/program"]
        end

        subgraph C["C 视觉分析模块
vision-event-module
HTTP: 8083"]
            C1["职责
1. 足球基础检测
2. 高活跃区域分析
3. 双机位关注区域输出
4. 多机位决策输出
5. 候选事件输出"]

            C2["输出内容
1. focus-regions
2. program-decision
3. event_candidates.json

关注区域频率：
200ms 一次

事件类型：
goal_candidate
shot_candidate
danger_attack_candidate
celebration_candidate"]
        end

        subgraph D["D 事件与集锦生成模块
highlight-generation-module
HTTP: 8084"]
            D1["职责
1. 读取 record_index.json
2. 读取 event_candidates.json
3. 裁切事件片段
4. 生成 full_highlight.mp4"]

            D2["第一版模式
仅支持：
全场精彩集锦

裁片规则示例：
goal: 前8s 后10s
shot: 前6s 后6s
danger_attack: 前5s 后5s
celebration: 前3s 后8s"]
        end

        subgraph E["E 平台与调度模块
platform-orchestration-module
HTTP: 8080"]
            E1["职责
1. 创建比赛
2. 开始比赛
3. 结束比赛
4. 触发集锦生成
5. 汇总状态
6. 展示主画面预览
7. 下载集锦结果"]

            E2["状态页最小展示项
cam_01 状态
cam_02 状态
当前推荐主画面机位
当前主画面输出状态
双路输入帧率
当前录制时长
最近错误
磁盘剩余空间"]
        end
    end

    %% =========================
    %% RTSP / Control Links
    %% =========================
    A -->|"RTSP 8554 main"| B
    A -->|"RTSP 8555 aux"| B
    A -->|"RTSP 8554 main"| C
    A -->|"RTSP 8555 aux"| C

    E -->|"HTTP 控制"| A
    E -->|"HTTP 控制"| B
    E -->|"HTTP 控制"| C
    E -->|"HTTP 控制"| D

    C -->|"POST /api/v1/record/matches/{match_id}/focus-regions"| B
    C -->|"POST /api/v1/record/matches/{match_id}/program-decision"| B

    %% =========================
    %% Data Layer
    %% =========================
    subgraph DATA["数据与文件层 Data Layer"]
        RAW["原始录像
{data_root}\\raw\\{match_id}\\cam_01.mp4
{data_root}\\raw\\{match_id}\\cam_02.mp4"]

        CUT["裁切与主画面录像
{data_root}\\program\\{match_id}\\cam_01_cut.mp4
{data_root}\\program\\{match_id}\\cam_02_cut.mp4
{data_root}\\program\\{match_id}\\program.mp4"]

        META["元数据
{data_root}\\metadata\\{match_id}\\record_index.json
{data_root}\\metadata\\{match_id}\\event_candidates.json
{data_root}\\metadata\\{match_id}\\focus_regions.json
{data_root}\\metadata\\{match_id}\\program_decision.json"]

        OUT["输出结果
{data_root}\\output\\{match_id}\\full_highlight.mp4"]

        LOG["日志
{data_root}\\logs\\{module_name}_{YYYYMMDD}.log"]
    end

    B --> RAW
    B --> CUT
    B --> META
    C --> META
    D --> OUT
    A --> LOG
    B --> LOG
    C --> LOG
    D --> LOG
    E --> LOG

    B -->|"record_index.json"| D
    C -->|"event_candidates.json"| D

    B -->|"主画面 RTSP 8560"| E
    E -->|"网页预览 / 结果下载"| VIEW

    %% =========================
    %% Notes / Constraints
    %% =========================
    subgraph NOTE["冻结约束 Frozen Constraints"]
        N1["系统部署：
Windows 原生单主机
当前不支持 Linux 生产部署"]

        N2["存储预算：
可用磁盘约 300GB
开始比赛前必须检查磁盘空间"]

        N3["MVP 明确不做：
球员号码识别
个人集锦
跨镜头球员身份统一
复杂公网直播平台能力"]

        N4["降级策略：
单路断流时系统继续运行
A 自动重连
B/C 对异常路降级
program_decision 超时回退 cam_01"]
    end -->
![](https://cdn.nlark.com/yuque/__mermaid_v3/02068fbaf6ab869b318a1349686005c3.svg)





### 2.1 硬件架构冻结
当前硬件架构固定为：

1. 海康威视彩色 GigE 工业相机 2 台，型号冻结为 `MV-CE050-30GC`
2. 镜头 2 只，参数冻结为 `6mm`、`C 接口`
3. 千兆 PoE 交换机 1 台
4. Windows 主机 1 台，GPU 为 `RTX 4080`
5. 可选控制端，通过浏览器访问后台
6. 当前 MVP 场景先跑通半场自动转播与全场集锦闭环

### 2.2 机位角色冻结
当前阶段采用两机位半场部署：

| camera_id | role | 安装位置 | 说明 |
| --- | --- | --- | --- |
| `cam_01` | 主机位 | 半场边线中路附近 | 负责半场全局覆盖、主叙事画面、默认主画面来源 |
| `cam_02` | 辅机位 | 底线角区附近 | 负责门前区域、边路推进、局部精彩片段补充 |


说明：

1. 当前版本允许先用 `cam_01` / `cam_02` 作为逻辑绑定标识。
2. 后续现场部署时，必须补充为 `camera_id -> serial_number -> role -> install_position` 的最终映射。
3. 禁止按相机枚举顺序隐式绑定机位角色。

### 2.3 软件架构冻结
系统采用**单主机集中式处理**。5 个模块统一运行在同一台 Windows 主机上：

| 模块编号 | 模块名称 | 英文名 | 负责人 |
| --- | --- | --- | --- |
| A | 采集与编码分发模块 | `ingest-streaming-module` | 工程师 A |
| B | 录像与主画面生成模块 | `record-program-module` | 工程师 B |
| C | 视觉分析模块 | `vision-event-module` | 工程师 C |
| D | 事件与集锦生成模块 | `highlight-generation-module` | 工程师 D |
| E | 平台与调度模块 | `platform-orchestration-module` | 工程师 E |


### 2.4 架构原则冻结
1. A 模块通过海康 MVS SDK 直接取两路工业相机视频。
2. A 模块统一向 B、C 输出两路标准化本机 RTSP 输入。
3. B 模块必须保证 `cam_01` 和 `cam_02` 两路画面都可裁切、都可生成可用画面。
4. C 模块必须输出双机位关注区域，并支持多机位决策。
5. D 模块继续消费既有 4 类候选事件，不新增事件类型。
6. E 模块统一调度 A、B、C、D，并展示双机位状态、主画面状态、最近错误与录制进度。
7. MVP 第一版优先保证稳定可落地，不为追求极限画质而破坏全链路实时性。

---

## 3. 模块定义与责任边界
### 3.1 模块 A：ingest-streaming-module
**负责**

1. 海康 MVS SDK 初始化
2. 两台 GigE 工业相机绑定
3. 工业相机参数加载
4. 连续取流
5. 基础编码压缩
6. 向 B、C 提供两路标准化视频输入
7. 流状态监控与异常上报
8. 单路断流自动重连

**不负责**

1. 原始录像落盘
2. 主画面裁切
3. 候选事件识别
4. 集锦生成
5. 后台页面

**强制要求**

1. A 模块必须固定输出 `cam_01` 与 `cam_02` 两路本机 RTSP 输入。
2. A 模块必须支持静态 IP 和固定网段。
3. A 模块必须支持启动时加载相机参数预设。
4. A 模块必须支持单路断流自动重连。

### 3.2 模块 B：record-program-module
**负责**

1. 两路原始录像保存
2. `cam_01` 裁切画面生成
3. `cam_02` 裁切画面生成
4. 主画面选择与输出
5. 主画面录像保存
6. 主画面预览输出
7. 文件归档与索引

**不负责**

1. 工业相机 SDK 取流
2. 候选事件识别
3. 集锦拼接
4. 平台页面

**强制要求**

1. B 模块必须能消费双机位关注区域。
2. B 模块必须能输出两路可用裁切画面。
3. B 模块必须支持基于视觉模块输出的多机位决策进行主画面选择。
4. 当多机位决策结果缺失时，B 模块必须回退到默认主机位策略。
5. B 模块必须输出标准化 `record_index.json` 供 D 模块消费。

### 3.3 模块 C：vision-event-module
**负责**

1. 消费 A 模块输出的两路本机 RTSP 视频流
2. 对视频帧执行 YOLO 实时足球/球员基础检测
3. 提取球坐标、运动强度、禁区活动等视觉信号
4. `cam_01` 关注区域输出
5. `cam_02` 关注区域输出
6. 多机位决策输出
7. 进球候选识别
8. 精彩片段候选识别
9. 候选事件结构化输出

**不负责**

1. 球员号码识别
2. 个人集锦支持
3. 录像文件保存
4. 主画面裁切实现
5. 集锦导出

**强制要求**

1. C 模块不能把 `cam_02` 只当作内部增强信号。
2. C 模块必须输出双机位可裁切结果。
3. C 模块必须输出多机位决策结果。
4. C 模块对 D 模块的事件输出仍保持既有 4 类候选事件，不新增事件类型。
5. C 模块必须按固定频率推送双机位关注区域。
6. C 模块内部架构必须按“视频流 -> YOLO 实时检测 -> 视觉信号提取 -> 事件/关注区域/导播决策输出”执行；该内部实现不得改变冻结 JSON 字段和接口路径。

### 3.4 模块 D：highlight-generation-module
**负责**

1. 读取录像索引文件
2. 读取候选事件文件
3. 裁切事件视频片段
4. 生成全场精彩集锦
5. 导出集锦文件
6. 返回任务状态与结果

**不负责**

1. 相机接入
2. 视频裁切预览
3. 视觉事件识别
4. 后台页面

**强制要求**

1. D 模块第一版只做“全场精彩集锦”。
2. D 模块优先使用主画面录像，必要时回查原始录像。
3. D 模块必须通过索引文件读取录像，不得自行扫目录推断。

### 3.5 模块 E：platform-orchestration-module
**负责**

1. 创建比赛
2. 开始比赛
3. 结束比赛
4. 调度 A、B、C、D
5. 汇总状态
6. 主画面预览页面
7. 集锦结果展示与下载
8. 配置管理

**不负责**

1. 相机 SDK 接入
2. 视频编码实现
3. 视觉推理
4. 集锦拼接实现

**强制要求**  
平台状态页必须支持展示：

1. `cam_01` 状态
2. `cam_02` 状态
3. 当前推荐主画面机位
4. 当前主画面输出状态
5. 双路输入帧率
6. 当前录制时长
7. 最近错误
8. 磁盘剩余空间

---

## 4. 进程、端口与本机地址约定
### 4.1 进程名冻结
| 模块 | Windows 可执行文件名 |
| --- | --- |
| A | `ingest_streaming_service.exe` |
| B | `record_program_service.exe` |
| C | `vision_event_service.exe` |
| D | `highlight_generation_service.exe` |
| E | `platform_orchestration_service.exe` |


### 4.2 HTTP 端口冻结
| 模块 | 默认端口 |
| --- | --- |
| E | `8080` |
| A | `8081` |
| B | `8082` |
| C | `8083` |
| D | `8084` |


### 4.3 本机 RTSP 端口冻结
| 用途 | 默认端口 | URI |
| --- | --- | --- |
| 主机位内部流 | `8554` | `rtsp://127.0.0.1:8554/main` |
| 辅机位内部流 | `8555` | `rtsp://127.0.0.1:8555/aux` |
| 主画面预览流 | `8560` | `rtsp://127.0.0.1:8560/program` |


### 4.4 地址冻结
所有模块在主机内部统一通过 `127.0.0.1` 访问。

---

## 5. Windows 文件系统与目录契约
### 5.1 数据根目录冻结
统一抽象变量名：

```plain
{data_root}
```

Windows 正式部署推荐值：

```plain
D:\football\data
```

### 5.2 最低目录结构冻结
```plain
{data_root}\raw
{data_root}\program
{data_root}\output
{data_root}\metadata
{data_root}\logs
{data_root}\temp
```

### 5.3 文件命名冻结
#### 原始录像
```plain
{data_root}\raw\{match_id}\cam_01.mp4
{data_root}\raw\{match_id}\cam_02.mp4
```

#### 主画面录像
```plain
{data_root}\program\{match_id}\program.mp4
```

#### 双机位裁切录像
```plain
{data_root}\program\{match_id}\cam_01_cut.mp4
{data_root}\program\{match_id}\cam_02_cut.mp4
```

#### 录像索引文件
```plain
{data_root}\metadata\{match_id}\record_index.json
```

#### 候选事件文件
```plain
{data_root}\metadata\{match_id}\event_candidates.json
```

#### 双机位关注区域文件（可选）
```plain
{data_root}\metadata\{match_id}\focus_regions.json
```

#### 多机位决策文件（可选）
```plain
{data_root}\metadata\{match_id}\program_decision.json
```

#### 集锦输出
```plain
{data_root}\output\{match_id}\full_highlight.mp4
```

### 5.4 存储预算冻结
当前 MVP 部署可用磁盘容量预期为 **300GB**。  
基于该约束：

1. 必须保留两路原始录像。
2. 默认保留主画面录像。
3. 双路裁切录像默认保留；若磁盘空间低于阈值，可由配置关闭或提前清理。
4. 必须在开始比赛前检查剩余空间。
5. 剩余空间低于安全阈值时，E 模块必须告警。

### 5.5 强制要求
1. 所有路径必须从配置文件读取。
2. 禁止在代码中直接写 `D:\football\data`。
3. 禁止模块自己创建第二套根目录。
4. 写文件后，必须在文件句柄关闭后再更新索引或返回结果。

---

## 6. 通信总原则
### 6.1 控制通信
所有控制类接口统一使用：

1. HTTP REST
2. JSON
3. 本机地址 `127.0.0.1`

### 6.2 视频通信
A 向 B、C 提供视频输入。  
MVP 第一版统一使用本机 RTSP 作为模块间视频输入协议。

### 6.3 文件通信
B、C、D、E 之间通过本地文件系统共享录像、元数据和集锦输出。

### 6.4 禁止事项
禁止：

1. 用共享文件代替控制接口
2. 用 JSON 传输视频内容
3. 在代码中写死路径、端口、IP
4. 绕开 E 模块自行定义任务调度协议

---

## 7. 数据格式总约定
### 7.1 JSON 字段命名冻结
统一使用 `snake_case`。

### 7.2 时间字段冻结
#### 绝对时间
+ 字段名：`timestamp_ms`
+ 类型：`integer`
+ 单位：毫秒
+ 含义：Windows 主机系统时间戳

#### 相对比赛时间
+ 字段名：`start_sec`、`end_sec`
+ 类型：`number`
+ 单位：秒
+ 含义：以“B 模块开始录制后首帧成功写入时间”为零点的相对比赛时间

### 7.3 通用响应格式冻结
所有 HTTP 接口统一返回：

```json
{
  "code": 0,
  "message": "ok",
  "data": {}
}
```

### 7.4 错误码冻结
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
| `1015` | 相机未绑定 |
| `1016` | 相机序列号冲突 |
| `1017` | 关注区域过期 |
| `1018` | 决策超时 |
| `1019` | 磁盘空间不足 |


### 7.5 状态值冻结
统一使用：

+ `idle`
+ `initializing`
+ `running`
+ `recording`
+ `processing`
+ `success`
+ `failed`
+ `stopped`
+ `degraded`

---

## 8. 模块间接口契约
### 8.1 模块 E -> 模块 A
#### 初始化采集任务
**方法**：`POST`  
**路径**：`/api/v1/ingest/matches/init`

**请求体**：

```json
{
  "match_id": "match_20260405_001",
  "cameras": [
    {
      "camera_id": "cam_01",
      "role": "main",
      "model": "MV-CE050-30GC",
      "lens": "6mm_C_mount",
      "stream_uri": "rtsp://127.0.0.1:8554/main"
    },
    {
      "camera_id": "cam_02",
      "role": "aux",
      "model": "MV-CE050-30GC",
      "lens": "6mm_C_mount",
      "stream_uri": "rtsp://127.0.0.1:8555/aux"
    }
  ],
  "network_config": {
    "mode": "static_ip",
    "subnet": "192.168.10.0/24"
  },
  "capture_config": {
    "internal_source_resolution": "5mp_native",
    "rtsp_output_resolution": "1920x1080",
    "fps": 25,
    "pixel_format": "bgr8_or_nv12",
    "video_codec": "h264"
  },
  "camera_param_strategy": {
    "load_from": "mvs_user_set_or_config",
    "trigger_mode": "continuous",
    "allow_runtime_page_edit": false
  }
}
```

### 8.2 模块 E -> 模块 B
#### 初始化录像与主画面任务
**方法**：`POST`  
**路径**：`/api/v1/record/matches/init`

**请求体**：

```json
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
    "program_root": "{data_root}\\program",
    "metadata_root": "{data_root}\\metadata"
  },
  "program_config": {
    "output_resolution": "1920x1080",
    "fps": 25,
    "default_mode": "follow_multi_focus_regions",
    "enable_dual_camera_cut": true,
    "aspect_ratio": "16:9",
    "crop_policy": "expand_then_clip",
    "min_camera_hold_ms": 2000,
    "smoothing_level": "light_anti_shake"
  },
  "record_config": {
    "container": "mp4",
    "video_codec": "h264",
    "save_raw_recordings": true,
    "save_cut_recordings": true,
    "save_program_recording": true
  }
}
```

### 8.3 模块 C -> 模块 B
#### 接口 1：批量更新双机位关注区域
**方法**：`POST`  
**路径**：`/api/v1/record/matches/{match_id}/focus-regions`

**请求体**：

```json
{
  "match_id": "match_20260405_001",
  "timestamp_ms": 1712323200123,
  "regions": [
    {
      "camera_id": "cam_01",
      "focus_region": {
        "x": 1200,
        "y": 650,
        "width": 1400,
        "height": 800
      },
      "source_type": "motion_cluster",
      "confidence": 0.87
    },
    {
      "camera_id": "cam_02",
      "focus_region": {
        "x": 320,
        "y": 180,
        "width": 900,
        "height": 600
      },
      "source_type": "ball_detection",
      "confidence": 0.91
    }
  ]
}
```

#### 接口 2：更新多机位决策结果
**方法**：`POST`  
**路径**：`/api/v1/record/matches/{match_id}/program-decision`

**请求体**：

```json
{
  "match_id": "match_20260405_001",
  "timestamp_ms": 1712323200123,
  "recommended_camera_id": "cam_02",
  "reason": "goal_area_activity_boosted",
  "confidence": 0.91
}
```

#### 冻结规则
1. `regions` 中必须包含 `cam_01` 和 `cam_02`。
2. 每路 `confidence` 必须在 `[0,1]`。
3. `width` 和 `height` 必须大于 0。
4. `x + width`、`y + height` 若越界，B 模块必须先裁剪到合法边界并记录 warning。
5. 若合法区域过小或不可用，B 模块必须回退默认区域。
6. 若 `program_decision` 超时，B 模块必须回退到默认主机位策略。
7. C 模块关注区域推送频率冻结为 **200ms 一次**。
8. C 模块对外推送多机位决策时，只在推荐机位变化、`reason` 变化或置信度跨阈值变化时推送。

### 8.4 模块 E -> 模块 C
#### 初始化视觉分析任务
**方法**：`POST`  
**路径**：`/api/v1/vision/matches/init`

**请求体**：

```json
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
  },
  "fusion_config": {
    "enable_dual_camera_focus_regions": true,
    "enable_program_decision": true,
    "focus_region_update_ms": 200,
    "aux_camera_role": "corner_goal_side"
  },
  "default_region_policy": {
    "cam_01": "half_field_center_safe_16_9",
    "cam_02": "goal_side_attack_area_16_9"
  }
}
```

#### C 模块内部处理链路
E 模块传入的 `streams` 是 C 模块实时检测的唯一视频入口。C 模块内部必须按以下顺序处理：

```plain
streams[].stream_uri
  -> OpenCV/视频输入层读取 RTSP 帧
  -> YOLO 实时检测足球/球员基础目标
  -> 提取球坐标、运动强度、禁区活动等信号
  -> 生成双机位关注区域、候选事件、多机位导播决策
```

该链路只约束 C 模块内部实现，不新增 E -> C 请求字段，不改变 C -> B、C -> D 的冻结输出格式。

### 8.5 模块 C -> 模块 D
#### 候选事件输出冻结
事件类型枚举保持不变：

+ `goal_candidate`
+ `shot_candidate`
+ `danger_attack_candidate`
+ `celebration_candidate`

#### 事件文件路径
```plain
{data_root}\metadata\{match_id}\event_candidates.json
```

### 8.6 模块 E -> 模块 D
#### 创建集锦任务
**方法**：`POST`  
**路径**：`/api/v1/highlight/matches/{match_id}/generate`

**请求体**：

```json
{
  "match_id": "match_20260405_001",
  "mode": "full_highlight",
  "record_index_path": "{data_root}\\metadata\\{match_id}\\record_index.json",
  "event_candidates_path": "{data_root}\\metadata\\{match_id}\\event_candidates.json",
  "clip_policy": {
    "goal_candidate": {
      "pre_sec": 8,
      "post_sec": 10
    },
    "shot_candidate": {
      "pre_sec": 6,
      "post_sec": 6
    },
    "danger_attack_candidate": {
      "pre_sec": 5,
      "post_sec": 5
    },
    "celebration_candidate": {
      "pre_sec": 3,
      "post_sec": 8
    }
  }
}
```

### 8.7 B 模块输出索引文件格式
#### `record_index.json`
```json
{
  "match_id": "match_20260405_001",
  "record_start_timestamp_ms": 1712323200000,
  "record_end_timestamp_ms": 1712328600000,
  "duration_sec": 5400,
  "video_codec": "h264",
  "container": "mp4",
  "fps": 25,
  "source_resolution": "5mp_native",
  "program_resolution": "1920x1080",
  "cam_01_raw_path": "D:\\football\\data\\raw\\match_20260405_001\\cam_01.mp4",
  "cam_02_raw_path": "D:\\football\\data\\raw\\match_20260405_001\\cam_02.mp4",
  "cam_01_cut_path": "D:\\football\\data\\program\\match_20260405_001\\cam_01_cut.mp4",
  "cam_02_cut_path": "D:\\football\\data\\program\\match_20260405_001\\cam_02_cut.mp4",
  "program_path": "D:\\football\\data\\program\\match_20260405_001\\program.mp4",
  "status": "success"
}
```

---

## 9. 状态机与错误码约定
### 9.1 模块状态流转
#### A 模块
`idle -> initializing -> running -> degraded / stopped / failed`

#### B 模块
`idle -> initializing -> recording -> degraded / stopped / failed`

#### C 模块
`idle -> initializing -> running -> degraded / stopped / failed`

#### D 模块
`idle -> processing -> success / failed`

#### E 模块
`idle -> running`

### 9.2 B 模块状态补充字段
B 模块状态返回中必须补充：

+ `cam_01_cut_status`
+ `cam_02_cut_status`
+ `current_program_camera_id`
+ `record_duration_sec`
+ `disk_free_gb`
+ `last_warning`

### 9.3 C 模块状态补充字段
C 模块状态返回中必须补充：

+ `focus_region_cam_01_ready`
+ `focus_region_cam_02_ready`
+ `last_program_decision_camera`
+ `last_focus_region_timestamp_ms`
+ `last_decision_timestamp_ms`

### 9.4 A 模块异常策略
1. 单路断流时，A 模块必须自动重连。
2. 5 秒内恢复成功，状态可从 `degraded` 回到 `running`。
3. 超过 5 秒未恢复，B 与 C 对该路进入降级策略。
4. 两路都不可用时，A 模块状态进入 `failed`。

### 9.5 B 模块降级策略
1. 某一路关注区域无效，只回退该路默认区域。
2. `program_decision` 超时，主画面回退 `cam_01`。
3. 单路 RTSP 输入异常，主画面不得直接中断；优先回退到可用机位。

---

## 10. 日志与追踪约定
1. 所有日志必须包含 `match_id`。
2. 涉及机位的日志必须包含 `camera_id`。
3. 涉及多机位决策的日志必须包含 `recommended_camera_id` 与 `reason`。
4. 日志文件命名冻结为：`{module_name}_{YYYYMMDD}.log`。

---

## 11. 联调顺序约定
### 第一步：A 模块单独联通
目标：

1. 两台工业相机可被识别
2. SDK 初始化成功
3. 两路视频可持续获取
4. 主机位和辅机位内部流可播放
5. 静态 IP 和网段配置正常

### 第二步：A + B 联调
目标：

1. 两路原始录像可保存
2. `cam_01` 裁切画面可生成
3. `cam_02` 裁切画面可生成
4. 主画面预览可查看
5. `record_index.json` 可生成

### 第三步：A + C 联调
目标：

1. 视觉模块可消费两路视频输入
2. 可输出双机位关注区域
3. 可输出第一版候选事件
4. 可输出第一版多机位决策结果

### 第四步：B + C 联调
目标：

1. B 模块可消费双机位关注区域
2. B 模块可消费多机位决策结果
3. 主画面可根据决策切换画面来源或保留双路裁切结果
4. 决策超时时可正确回退 `cam_01`

### 第五步：C + D 联调
目标：

1. D 能读取候选事件
2. D 能读取录像索引
3. D 能生成第一版全场精彩集锦

### 第六步：E 接入所有模块
目标：

1. 后台可创建比赛
2. 后台可开始比赛与结束比赛
3. 后台可查看主画面预览
4. 后台可触发集锦生成
5. 后台可查看双机位状态与当前推荐机位
6. 后台可下载集锦结果

---

## 12. 变更控制规则
本次 `v1.2 Frozen` 正式冻结以下内容：

1. 海康工业相机接入方式
2. 本机 RTSP 作为模块间视频协议
3. 双机位关注区域输出
4. 多机位决策输出
5. 双机位可裁切要求
6. 1080p25 主画面输出规格
7. 16:9 裁切输出规则
8. 4 类候选事件枚举
9. 全场精彩集锦模式
10. `record_index.json` 结构

---

## 13. 责任归属规则
### A 模块责任边界
工业相机枚举失败、SDK 初始化失败、取流失败、内部视频输入不可用、静态 IP 配置异常，责任优先归 A。

### B 模块责任边界
双路裁切画面不可生成、主画面不生成、画面切换异常、录像文件损坏、索引文件缺失或格式错误，责任优先归 B。

### C 模块责任边界
双机位关注区域不输出、候选事件为空、多机位决策缺失或格式错误、关注区域过期未更新，责任优先归 C。

### D 模块责任边界
有录像与事件输入但集锦不生成、输出文件损坏、片段拼接错误，责任优先归 D。

### E 模块责任边界
后台无法调度、状态不显示、任务无法触发、下载入口无效、空间告警未正确展示，责任优先归 E。

---

## 14. 性能与范围冻结
### 14.1 性能目标
1. 双路 `1920x1080@25fps` 标准化输入稳定运行。
2. 双路原始录像稳定写盘。
3. 双路裁切与主画面输出实时运行。
4. 90 分钟比赛结束后，首版全场集锦应在 20 分钟内生成完成。

### 14.2 MVP 明确不做
1. 不做球员号码识别
2. 不做个人集锦
3. 不做跨镜头球员身份统一
4. 不做复杂公网大规模直播平台能力
5. 不做多机位复杂导播语言控制
6. 不做移动端 App

---

## 15. 冻结结论
本版本冻结后，以下事项视为正式执行要求：

1. `cam_02` 必须能裁切。
2. 双机位画面都要有可用性。
3. 视觉模块必须输出多机位决策结果。
4. 采集模块必须基于海康 MVS SDK 接入两台 `MV-CE050-30GC`。
5. 模块间视频输入协议冻结为本机 RTSP。
6. 主画面输出规格冻结为 `1920x1080@25fps`。
7. 集锦生成第一版只做“全场精彩集锦”。

本版本可作为双机位半场 MVP 的正式开发契约。

