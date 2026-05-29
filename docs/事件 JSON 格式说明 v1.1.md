# 事件 JSON 格式说明 v1.1

**文档版本**: v1.1  
**最后更新**: 2026-04-24  
**适用模块**: `vision-event-module` → `highlight-generation-module`  
**适用架构**: Windows 原生单主机集中式架构  
**适用场景**: 候选事件输出、精彩集锦生成数据源

---

## 1. 文档目的

本文档定义足球赛事自动转播系统中**候选事件（Event Candidates）** 的 JSON 格式规范。  
视觉分析模块通过此格式向集锦生成模块输出候选事件数据，用于：

1. 进球候选 → 进球集锦生成  
2. 射门候选 → 射门集锦生成  
3. 危险进攻候选 → 危险进攻片段标记  
4. 庆祝候选 → 庆祝片段标记  

---

## 2. 核心原则

当前版本冻结以下原则：

1. 事件类型枚举仅支持 4 种候选类型，不允许私自新增  
2. 每个事件必须包含完整的时间范围（start_sec / end_sec）  
3. 每个事件必须关联一个主导来源机位（camera_id）  
4. 置信度必须在 `[0, 1]` 范围内  
5. 事件 ID 必须遵循 `evt_` + 至少 4 位数字的格式  
6. 若事件由双机位融合产生，记录主导来源机位  

---

## 3. 事件类型枚举

当前冻结为以下 4 种事件类型：

| 枚举值 | 字符串表示 | 说明 |
| --- | --- | --- |
| `GOAL_CANDIDATE` | `goal_candidate` | 进球候选 |
| `SHOT_CANDIDATE` | `shot_candidate` | 射门候选 |
| `DANGER_ATTACK_CANDIDATE` | `danger_attack_candidate` | 危险进攻候选 |
| `CELEBRATION_CANDIDATE` | `celebration_candidate` | 庆祝候选 |

### 冻结说明

当前版本不允许新增：

+ `corner_candidate`
+ `foul_candidate`
+ `offside_candidate`
+ 其他自定义事件类型

若后续扩展，必须升级文档版本。

---

## 4. 单条事件结构

### 4.1 JSON 格式

```json
{
  "event_id": "evt_0001",
  "event_type": "goal_candidate",
  "start_sec": 312.4,
  "end_sec": 320.6,
  "confidence": 0.92,
  "camera_id": "cam_02"
}
```

### 4.2 字段说明

| 字段名 | 类型 | 必填 | 约束 | 说明 |
| --- | --- | --- | --- | --- |
| `event_id` | string | 是 | 格式：`evt_` + 至少 4 位数字 | 事件唯一标识 |
| `event_type` | string | 是 | 4 个冻结枚举值之一 | 事件类型 |
| `start_sec` | number | 是 | `>= 0` | 相对比赛开始时间（秒） |
| `end_sec` | string | 是 | `> start_sec` | 相对比赛结束时间（秒） |
| `confidence` | number | 是 | `[0, 1]` | 置信度 |
| `camera_id` | string | 是 | `cam_01` 或 `cam_02` | 事件主导来源机位 |

### 4.3 字段详细说明

#### event_id

- 格式必须为 `evt_` 前缀 + 至少 4 位数字，如 `evt_0001`、`evt_0123`
- 在同一场比赛内必须唯一
- 数字部分按事件检测顺序递增

#### event_type

- 必须是 4 个冻结枚举值之一（见第 3 节）
- 使用字符串表示（如 `goal_candidate`），不使用枚举序号

#### start_sec / end_sec

- 相对于比赛开始时间的秒数，浮点数
- `start_sec >= 0`
- `end_sec > start_sec`
- 精度建议保留 1 位小数

#### confidence

- 置信度值，范围 `[0, 1]`
- `1.0` 表示完全确定，`0.0` 表示完全不确定
- 下游模块可根据置信度阈值过滤事件

#### camera_id

- 表示当前事件主要依据的来源机位
- `cam_01`：事件主要由主机位信号驱动
- `cam_02`：事件主要由辅机位信号驱动
- 若事件由双机位融合产生，推荐记录**主导来源机位**

---

## 5. 事件查询响应结构

### 5.1 JSON 格式

```json
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
      },
      {
        "event_id": "evt_0002",
        "event_type": "shot_candidate",
        "start_sec": 455.0,
        "end_sec": 462.3,
        "confidence": 0.78,
        "camera_id": "cam_01"
      }
    ]
  }
}
```

### 5.2 字段说明

| 字段名 | 类型 | 说明 |
| --- | --- | --- |
| `code` | integer | 响应码，`0` 表示成功 |
| `message` | string | 响应消息 |
| `data.match_id` | string | 比赛 ID |
| `data.events` | array | 候选事件列表 |

---

## 6. 事件类型说明

### 6.1 goal_candidate（进球候选）

综合以下信号生成：

1. `cam_01` 的全局进攻趋势
2. `cam_01` 的球门区域活动变化
3. `cam_02` 的门前近景活动增强
4. `cam_02` 的球门线附近球体接近线区域信号
5. `cam_02` 的小禁区内高强度动作变化

典型时间窗口：进球前后各 3-5 秒。

### 6.2 shot_candidate（射门候选）

综合以下信号生成：

1. `cam_01` 的高速球与门前活动
2. `cam_02` 的门前近景动作增强
3. `cam_02` 的小禁区局部高活跃度

典型时间窗口：射门动作前后各 2-3 秒。

### 6.3 danger_attack_candidate（危险进攻候选）

主要由 `cam_01` 提供，`cam_02` 作为门前增强参考。

典型时间窗口：危险进攻持续期间。

### 6.4 celebration_candidate（庆祝候选）

以 `cam_01` 的全局节奏变化为主，辅以 `cam_02` 的门前局部高运动强度。

典型时间窗口：进球后庆祝动作期间（约 10-30 秒）。

---

## 7. HTTP API 端点

### 7.1 查询候选事件

**请求方法**: `GET`  
**请求路径**:

```plain
/api/v1/vision/matches/{match_id}/event-candidates
```

**成功响应**（见第 5.1 节）。

**失败响应**:

```json
{
  "code": 1002,
  "message": "资源未初始化",
  "data": {}
}
```

### 7.2 错误码

| code | 含义 |
| --- | --- |
| `0` | 成功 |
| `1001` | 参数错误 |
| `1002` | 资源未初始化 |
| `1003` | 输入源不可用 |
| `1004` | 视频流异常 |

---

## 8. 事件融合逻辑说明

### 8.1 双机位融合策略

| 事件类型 | cam_01 权重 | cam_02 权重 | 说明 |
| --- | --- | --- | --- |
| `goal_candidate` | 高 | 高 | 双机位协同确认 |
| `shot_candidate` | 中 | 高 | 辅机位门前增强为主 |
| `danger_attack_candidate` | 高 | 低 | 主机位全局趋势为主 |
| `celebration_candidate` | 高 | 中 | 主机位节奏变化为主 |

### 8.2 camera_id 选择规则

- 若事件主要由 `cam_01` 信号驱动 → `camera_id = "cam_01"`
- 若事件主要由 `cam_02` 信号驱动 → `camera_id = "cam_02"`
- 若双机位信号相当 → 优先记录 `cam_01`（主机位）

---

## 9. 完整示例

### 9.1 进球候选

```json
{
  "event_id": "evt_0001",
  "event_type": "goal_candidate",
  "start_sec": 312.4,
  "end_sec": 320.6,
  "confidence": 0.92,
  "camera_id": "cam_02"
}
```

说明：第 312.4 秒检测到进球候选事件，持续至 320.6 秒，置信度 0.92，主要由辅机位（门前近景）信号驱动。

### 9.2 射门候选

```json
{
  "event_id": "evt_0002",
  "event_type": "shot_candidate",
  "start_sec": 455.0,
  "end_sec": 462.3,
  "confidence": 0.78,
  "camera_id": "cam_01"
}
```

说明：第 455.0 秒检测到射门候选事件，持续至 462.3 秒，置信度 0.78，主要由主机位信号驱动。

### 9.3 危险进攻候选

```json
{
  "event_id": "evt_0003",
  "event_type": "danger_attack_candidate",
  "start_sec": 580.2,
  "end_sec": 605.8,
  "confidence": 0.65,
  "camera_id": "cam_01"
}
```

说明：第 580.2 秒检测到危险进攻候选事件，持续至 605.8 秒，置信度 0.65，由主机位全局趋势驱动。

### 9.4 庆祝候选

```json
{
  "event_id": "evt_0004",
  "event_type": "celebration_candidate",
  "start_sec": 320.6,
  "end_sec": 345.0,
  "confidence": 0.85,
  "camera_id": "cam_01"
}
```

说明：进球后第 320.6 秒检测到庆祝候选事件，持续至 345.0 秒，置信度 0.85，由主机位节奏变化驱动。

---

## 10. 校验规则

### 10.1 必填字段校验

单条事件必须包含以下字段：

+ `event_id`
+ `event_type`
+ `start_sec`
+ `end_sec`
+ `confidence`
+ `camera_id`

### 10.2 值域校验

#### event_id

- 必须以 `evt_` 开头
- 后接至少 4 位数字
- 示例：`evt_0001`、`evt_0123`、`evt_9999`

#### event_type

必须属于：

+ `goal_candidate`
+ `shot_candidate`
+ `danger_attack_candidate`
+ `celebration_candidate`

#### start_sec

- 必须 `>= 0`
- 必须为数字（整数或浮点数）

#### end_sec

- 必须 `> start_sec`
- 必须为数字（整数或浮点数）

#### confidence

- 必须满足 `0 <= confidence <= 1`

#### camera_id

必须属于：

+ `cam_01`
+ `cam_02`

### 10.3 唯一性校验

- `event_id` 在同一场比赛内必须唯一

---

## 11. 下游消费建议

### 11.1 集锦生成模块

- 根据 `event_type` 选择对应的集锦生成策略
- 根据 `confidence` 设置过滤阈值（建议进球 ≥ 0.7，射门 ≥ 0.6）
- 使用 `start_sec` / `end_sec` 定位视频片段
- 根据 `camera_id` 决定主画面机位

### 11.2 置信度阈值参考

| 事件类型 | 建议最低置信度 | 说明 |
| --- | --- | --- |
| `goal_candidate` | 0.7 | 进球需高置信度 |
| `shot_candidate` | 0.6 | 射门中等置信度 |
| `danger_attack_candidate` | 0.5 | 危险进攻低阈值 |
| `celebration_candidate` | 0.6 | 庆祝中等置信度 |

---

## 12. 冻结要求

本版本冻结后，以下内容不允许私自修改：

1. 事件类型枚举（4 种类型）
2. 单条事件 JSON 结构（6 个字段）
3. `event-candidates` 查询接口路径
4. 事件 ID 格式规则
5. 基础校验规则
6. 通用响应格式 `{ code, message, data }`

---

## 13. 版本历史

| 版本 | 日期 | 变更说明 |
| --- | --- | --- |
| v1.0 | — | 无此前版本 |
| v1.1 | 2026-04-24 | 初始版本，定义 4 种候选事件类型及完整 JSON 格式规范 |
