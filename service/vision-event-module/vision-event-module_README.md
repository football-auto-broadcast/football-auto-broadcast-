# vision-event-module 视觉分析模块
## 模块名称
视觉分析模块

## 英文名
`vision-event-module`

---

## 概述
视觉分析模块负责从比赛视频中提取与自动转播和精彩集锦相关的基础视觉信号。

本模块当前采用**双机位协同分析、双机位可裁切输出、多机位决策驱动**的实现策略。

在新架构下，本模块运行于 RTX 4080 主机本地，直接消费采集模块输出的视频输入，不依赖跨节点视频拉流。

本模块不负责：

1. 球员号码识别  
2. 球员身份区分  
3. 个人集锦支持  
4. 最终主画面录制  
5. 集锦拼接导出  
6. 平台页面渲染

本模块负责：

1. 为录像与主画面生成模块输出 `cam_01` 和 `cam_02` 的关注区域  
2. 为录像与主画面生成模块输出多机位决策结果  
3. 为集锦生成模块输出候选事件数据  
4. 通过双机位协同提升门前事件与小禁区精彩瞬间的识别质量  
5. 保证双机位画面都具备可裁切、可使用的视觉基础能力

---

## 当前双机位职责定义
### cam_01 高位广角主机位
`cam_01` 的职责固定如下：

1. 负责全场覆盖  
2. 负责自动转播主画面的全局关注区域  
3. 负责全场运动趋势分析  
4. 负责主时间线上的候选事件基础检测  
5. 作为默认主画面来源机位

### cam_02 门线延长线辅机位
`cam_02` 的职责固定如下：

1. 负责门前近景观察  
2. 负责球门区域活动增强  
3. 负责进球候选辅助确认  
4. 负责小禁区精彩瞬间候选增强  
5. 负责输出本机位可裁切关注区域  
6. 作为门前细节叙事机位参与主画面选择

---

## 设计原则
### 原则 1：双机位都必须可裁切
当前版本中：

+ `cam_01` 必须输出可用关注区域
+ `cam_02` 必须输出可用关注区域

不允许出现“只有主机位能裁切、辅机位只是辅助判断”的实现。

### 原则 2：双机位都必须具备画面可用性
视觉模块输出的双机位关注区域，必须能被下游模块直接用于生成：

+ `cam_01` 裁切画面
+ `cam_02` 裁切画面

### 原则 3：视觉模块必须支持多机位决策
视觉模块必须输出一份多机位决策结果，用于指导主画面模块决定当前更适合使用哪一路机位。

### 原则 4：辅机位不是内部隐藏信号
`cam_02` 不再只是视觉内部增强信号。  
它必须是一个**正式对外可用的裁切机位**，并参与多机位决策逻辑。

---

## 工程结构
```latex
service/vision-event-module/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── event_types.hpp             # 事件类型枚举与数据结构
│   ├── frame_input.hpp             # 输入帧格式定义
│   ├── focus_region.hpp            # 单路关注区域定义
│   ├── multi_focus_region.hpp      # 双机位关注区域集合定义
│   ├── program_decision.hpp        # 多机位决策结构定义
│   ├── json_output.hpp             # JSON 输出结构
│   ├── fusion_policy.hpp           # 双机位融合策略定义
│   └── camera_role.hpp             # 机位角色定义
├── src/
│   ├── main.cpp
│   ├── service.cpp
│   ├── http_server.cpp
│   ├── event_types.cpp
│   ├── focus_region.cpp
│   ├── multi_focus_region.cpp
│   ├── program_decision.cpp
│   ├── json_output.cpp
│   ├── fusion_policy.cpp
│   └── camera_role.cpp
├── inference/
│   ├── include/
│   │   ├── ball_detector.hpp
│   │   ├── motion_analyzer.hpp
│   │   ├── event_classifier.hpp
│   │   ├── goal_assist_analyzer.hpp
│   │   └── box_activity_analyzer.hpp
│   └── src/
│       ├── ball_detector.cpp
│       ├── motion_analyzer.cpp
│       ├── event_classifier.cpp
│       ├── goal_assist_analyzer.cpp
│       └── box_activity_analyzer.cpp
├── config/
│   └── default_config.yaml
└── tests/
    ├── test_event_types.cpp
    ├── test_focus_region.cpp
    ├── test_multi_focus_region.cpp
    └── test_fusion_policy.cpp
```

## 输入
本模块接收以下输入：

1.  主机位视频输入 `cam_01`
2.  辅机位视频输入 `cam_02`
3.  视觉参数配置 
4.  事件规则配置 
5.  双机位融合策略配置 

---

## 输出
本模块输出以下结果：

1. `cam_01` 关注区域 
2. `cam_02` 关注区域 
3.  双机位关注区域集合 
4.  多机位决策结果 
5.  进球候选事件 
6.  精彩片段候选事件 
7.  候选事件 JSON 数据 
8.  模块状态与日志 

---

## 输入帧格式
### InputFrame 结构
| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `image` | `cv::Mat` | 图像数据，默认 BGR8 |
| `camera_id` | `std::string` | 相机 ID，`cam_01`<br/> 或 `cam_02` |
| `match_id` | `std::string` | 比赛 ID |
| `timestamp_ms` | `int64_t` | 帧时间戳，毫秒 |
| `frame_index` | `int64_t` | 帧序号，从 0 开始 |
| `width` | `int` | 帧宽度 |
| `height` | `int` | 帧高度 |
| `fps` | `int` | 帧率 |
| `format` | `FrameFormat` | 帧格式 |


### 帧格式枚举
```plain
enum class FrameFormat {
    BGR8 = 0,
    RGB8 = 1,
    GRAY8 = 2,
    NV12 = 3,
    YUV420P = 4
};
```

---

## 视觉模块内部逻辑
## 1. 双机位关注区域生成
### cam_01 关注区域
`cam_01` 关注区域由以下信号综合生成：

1.  足球位置估计 
2.  全场运动聚集区域 
3.  进攻方向 
4.  禁区与门前活动变化 

目标是让 `cam_01` 保持全场叙事能力与主画面基础稳定性。

### cam_02 关注区域
`cam_02` 关注区域由以下信号综合生成：

1.  球门区域局部活动 
2.  球与球门线附近位置关系 
3.  小禁区内高强度动作变化 
4.  门前人群聚集程度 

目标是让 `cam_02` 在门前近景和小禁区高价值片段中具备独立可裁切价值。

### 关注区域输出原则
当前版本冻结为：

1.  必须同时输出 `cam_01` 与 `cam_02` 两路关注区域 
2.  任一路关注区域失效时，只允许该路回退默认区域 
3.  不允许因为某一路异常而让另一机位停止输出 

---

## 2. 多机位决策逻辑
视觉模块必须生成一份多机位决策结果，用于指导主画面模块选择当前推荐机位。

### 决策目标
在以下场景中，系统应能明确区分更适合使用哪一路机位：

1.  全场推进阶段 
2.  门前进攻阶段 
3.  小禁区精彩瞬间阶段 
4.  进球候选增强阶段 
5.  默认回退阶段 

### 推荐机位示例
+  普通全场推进：优先 `cam_01`
+  门前活动明显增强：可推荐 `cam_02`
+  小禁区高活跃度：可推荐 `cam_02`
+  决策不稳定：回退 `cam_01`

---

## 3. 候选事件融合逻辑
### goal_candidate
综合以下信号生成：

1. `cam_01` 的全局进攻趋势 
2. `cam_01` 的球门区域活动变化 
3. `cam_02` 的门前近景活动增强 
4. `cam_02` 的球门线附近球体接近线区域信号 
5. `cam_02` 的小禁区内高强度动作变化 

### shot_candidate
综合以下信号生成：

1. `cam_01` 的高速球与门前活动 
2. `cam_02` 的门前近景动作增强 
3. `cam_02` 的小禁区局部高活跃度 

### danger_attack_candidate
主要由 `cam_01` 提供，`cam_02` 作为门前增强参考。

### celebration_candidate
以 `cam_01` 的全局节奏变化为主，辅以 `cam_02` 的门前局部高运动强度。

---

## 输出 JSON 结构
### 通用响应格式
所有 HTTP API 统一返回：

```plain
{
  "code": 0,
  "message": "ok",
  "data": {}
}
```

### 错误码
| code | 含义 |
| --- | --- |
| `0` | 成功 |
| `1001` | 参数错误 |
| `1002` | 资源未初始化 |
| `1003` | 输入源不可用 |
| `1004` | 视频流异常 |
| `1012` | 工业相机 SDK 初始化失败 |
| `1013` | 工业相机枚举失败 |
| `1014` | 相机取流失败 |


---

## 事件类型枚举
### EventType
```plain
enum class EventType {
    GOAL_CANDIDATE = 0,
    SHOT_CANDIDATE = 1,
    DANGER_ATTACK_CANDIDATE = 2,
    CELEBRATION_CANDIDATE = 3
};
```

### Event 数据结构
```plain
{
  "event_id": "evt_0001",
  "event_type": "goal_candidate",
  "start_sec": 312.4,
  "end_sec": 320.6,
  "confidence": 0.92,
  "camera_id": "cam_02"
}
```

| 字段 | 类型 | 说明 | 约束 |
| --- | --- | --- | --- |
| `event_id` | string | 事件唯一 ID | 格式：`evt_`<br/> + 至少 4 位数字 |
| `event_type` | string | 事件类型 | 只能是 4 个冻结枚举之一 |
| `start_sec` | number | 相对比赛开始时间，秒 | `>= 0` |
| `end_sec` | number | 相对比赛结束时间，秒 | `> start_sec` |
| `confidence` | number | 置信度 | `[0, 1]` |
| `camera_id` | string | 事件主导来源机位 | `cam_01`<br/> 或 `cam_02` |


### 事件说明
`camera_id` 表示当前事件主要依据的来源机位。  
若事件主要由双机位融合产生，推荐记录主导来源机位。

### 事件查询响应
`GET /api/v1/vision/matches/{match_id}/event-candidates`

```plain
{
  "code": 0,
  "message": "ok",
  "data": {
    "match_id": "match_20260405_001",
    "events": [
      {
        "event_id": "evt_0001",
        "event_type": "goal_candidate",
        "start_sec": 312.4,
        "end_sec": 320.6,
        "confidence": 0.92,
        "camera_id": "cam_02"
      }
    ]
  }
}
```

---

## 双机位关注区域输出格式
### MultiFocusRegion 结构
```plain
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

### 冻结规则
1. `regions` 中必须同时包含 `cam_01` 与 `cam_02`
2.  每路机位必须有独立关注区域 
3.  每路 `confidence` 必须在 `[0,1]`
4. `width` 和 `height` 必须大于 0 

### 推送给 record-program-module
**方法**：`POST`  
**路径**：

```plain
/api/v1/record/matches/{match_id}/focus-regions
```

---

## 多机位决策输出格式
### ProgramDecision 结构
```plain
{
  "match_id": "match_20260405_001",
  "timestamp_ms": 1712323200123,
  "recommended_camera_id": "cam_02",
  "reason": "goal_area_activity_boosted",
  "confidence": 0.91
}
```

| 字段 | 类型 | 说明 | 约束 |
| --- | --- | --- | --- |
| `match_id` | string | 比赛 ID | 非空 |
| `timestamp_ms` | integer | 时间戳 | >= 0 |
| `recommended_camera_id` | string | 推荐机位 | `cam_01`<br/> 或 `cam_02` |
| `reason` | string | 推荐原因 | 冻结枚举值 |
| `confidence` | number | 置信度 | `[0,1]` |


### reason 推荐值
+ `global_play_tracking`
+ `goal_area_activity_boosted`
+ `six_yard_box_highlight`
+ `default_main_camera`
+ `aux_camera_fallback`

### 推送给 record-program-module
**方法**：`POST`  
**路径**：

```plain
/api/v1/record/matches/{match_id}/program-decision
```

---

## 模块状态
### ModuleStatus
```plain
{
  "match_id": "match_20260405_001",
  "status": "running",
  "camera_main_status": "online",
  "camera_aux_status": "online",
  "fps_main": 25,
  "fps_aux": 25,
  "events_detected": 12,
  "focus_region_cam_01_ready": true,
  "focus_region_cam_02_ready": true,
  "last_program_decision_camera": "cam_02",
  "error_message": ""
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `status` | string | 模块状态 |
| `camera_main_status` | string | 主机位状态 |
| `camera_aux_status` | string | 辅机位状态 |
| `fps_main` | integer | 主机位帧率 |
| `fps_aux` | integer | 辅机位帧率 |
| `events_detected` | integer | 已检测事件数 |
| `focus_region_cam_01_ready` | boolean | 主机位关注区域是否就绪 |
| `focus_region_cam_02_ready` | boolean | 辅机位关注区域是否就绪 |
| `last_program_decision_camera` | string | 最近一次推荐机位 |
| `error_message` | string | 最近错误信息 |


### 状态枚举
```plain
enum class ModuleState {
    IDLE = 0,
    INITIALIZING = 1,
    RUNNING = 2,
    STOPPED = 3,
    FAILED = 4
};
```

---

## HTTP API 端点
| 方法 | 路径 | 说明 | 端口 |
| --- | --- | --- | --- |
| `POST` | `/api/v1/vision/matches/init` | 初始化视觉分析任务 | 8083 |
| `POST` | `/api/v1/vision/matches/{match_id}/start` | 开始视觉分析 | 8083 |
| `POST` | `/api/v1/vision/matches/{match_id}/stop` | 停止视觉分析 | 8083 |
| `GET` | `/api/v1/vision/matches/{match_id}/status` | 查询视觉状态 | 8083 |
| `GET` | `/api/v1/vision/matches/{match_id}/event-candidates` | 查询候选事件 | 8083 |
| `GET` | `/api/v1/vision/matches/{match_id}/focus-regions` | 查询双机位关注区域 | 8083 |
| `GET` | `/api/v1/vision/matches/{match_id}/program-decision` | 查询多机位决策结果 | 8083 |


---

## 构建说明
### 依赖
+  CMake ≥ 3.16 
+  C++17 编译器 
+  OpenCV ≥ 4.5 
+  ONNX Runtime ≥ 1.10 
+  可选 CUDA Toolkit，用于 GPU 加速 

### 编译
```plain
cd src/vision-event-module
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### 运行
```plain
./vision_event_service --config ../config/default_config.yaml
```

---

## 配置示例
```plain
match_id: "match_20260405_001"

input:
  main_camera:
    stream_uri: "rtsp://127.0.0.1:8554/main"
    camera_id: "cam_01"
    role: "main"
  aux_camera:
    stream_uri: "rtsp://127.0.0.1:8555/aux"
    camera_id: "cam_02"
    role: "aux"

fusion:
  enable_dual_camera_fusion: true
  enable_dual_camera_focus_regions: true
  enable_program_decision: true
  goal_candidate_use_aux_boost: true
  shot_candidate_use_aux_boost: true
  six_yard_box_enhancement: true
  aux_camera_role: "goal_line_extension"

output:
  focus_region_update_ms: 200
  push_dual_regions: true
  push_program_decision: true
  event_push_enabled: true

http:
  port: 8083
  host: "127.0.0.1"
```

---

## 成功验收标准
1.  能输出 `cam_01` 可用关注区域 
2.  能输出 `cam_02` 可用关注区域 
3.  双机位关注区域都具备独立裁切价值 
4.  能输出多机位决策结果 
5.  能输出进球候选事件 
6.  能输出精彩片段候选事件 
7. `cam_02` 能正式参与主画面选择基础逻辑 
8.  输出的事件结构可被集锦模块直接消费 
9.  在双机位决策场景下保持基础可用性 
10.  在单主机集中式架构下可稳定运行 

