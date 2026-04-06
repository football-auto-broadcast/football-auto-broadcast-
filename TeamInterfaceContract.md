# Team Interface Contract v1
## 足球赛事自动转播系统团队接口约定文档

---

## 目录

- [1. 文档信息](#1-文档信息)
- [2. 文档目的](#2-文档目的)
- [3. 系统模块定义](#3-系统模块定义)
- [4. 模块职责边界](#4-模块职责边界)
  - [4.1 模块 A：采集与编码分发模块](#41-模块-a采集与编码分发模块)
  - [4.2 模块 B：录像与主画面生成模块](#42-模块-b录像与主画面生成模块)
  - [4.3 模块 C：视觉分析模块](#43-模块-c视觉分析模块)
  - [4.4 模块 D：事件与集锦生成模块](#44-模块-d事件与集锦生成模块)
  - [4.5 模块 E：平台与调度模块](#45-模块-e平台与调度模块)
- [5. 部署与网络拓扑约定](#5-部署与网络拓扑约定)
- [6. 通信方式约定](#6-通信方式约定)
- [7. 数据格式总约定](#7-数据格式总约定)
- [8. 模块间接口契约](#8-模块间接口契约)
  - [8.1 模块 E -> 模块 A](#81-模块-e---模块-a)
  - [8.2 模块 E -> 模块 B](#82-模块-e---模块-b)
  - [8.3 模块 C -> 模块 B](#83-模块-c---模块-b)
  - [8.4 模块 E -> 模块 C](#84-模块-e---模块-c)
  - [8.5 模块 C -> 模块 D](#85-模块-c---模块-d)
  - [8.6 模块 E -> 模块 D](#86-模块-e---模块-d)
  - [8.7 模块 D -> 模块 E](#87-模块-d---模块-e)
- [9. 文件路径契约](#9-文件路径契约)
- [10. 日志契约](#10-日志契约)
- [11. 联调顺序约定](#11-联调顺序约定)
- [12. 变更控制规则](#12-变更控制规则)
- [13. 责任归属规则](#13-责任归属规则)
- [14. Mermaid 通信链路图](#14-mermaid-通信链路图)
- [15. 可行性校验](#15-可行性校验)
- [16. 最终结论](#16-最终结论)

---

## 1. 文档信息

**文档名称**：Team Interface Contract  
**版本**：v1.0  
**适用阶段**：MVP 第一版  
**适用项目**：足球赛事自动转播系统  
**冻结时间要求**：第 1 周结束前冻结 v1  
**适用团队**：5 位工程师并行开发与联调

---

## 2. 文档目的

本文件用于定义五个工程模块之间的：

1. 功能边界
2. 输入输出约定
3. 接口路径
4. 数据字段
5. 状态与错误码
6. 联调顺序
7. 责任归属
8. 变更规则

本文件的目标是确保五位工程师可以在第 2 周开始并行开发，并在第 4 到第 7 周顺利联调和中期展示。

---

## 3. 系统模块定义

本项目共分为 5 个模块。

| 模块编号 | 模块名称 | 英文名 | 部署节点 | 负责人 |
|---|---|---|---|---|
| A | 采集与编码分发模块 | `ingest-streaming-module` | PC1 | 工程师 A |
| B | 录像与主画面生成模块 | `record-program-module` | PC1 | 工程师 B |
| C | 视觉分析模块 | `vision-event-module` | PC2 | 工程师 C |
| D | 事件与集锦生成模块 | `highlight-generation-module` | PC3 | 工程师 D |
| E | 平台与调度模块 | `platform-orchestration-module` | PC4 | 工程师 E |

---

## 4. 模块职责边界

### 4.1 模块 A：采集与编码分发模块

负责：

1. 两路相机视频接入
2. 视频编码压缩
3. 视频流分发
4. 流状态上报
5. 向局域网提供标准流地址

不负责：

1. 录像文件管理
2. 主画面裁切
3. 视觉分析
4. 集锦生成
5. 平台调度

---

### 4.2 模块 B：录像与主画面生成模块

负责：

1. 两路原始录像保存
2. 主画面生成
3. 主画面裁切和平滑
4. 主画面录制
5. 视频文件归档与索引

不负责：

1. 相机直接接入
2. 编码分发
3. 事件识别
4. 集锦拼接
5. 后台调度

---

### 4.3 模块 C：视觉分析模块

负责：

1. 主画面关注区域输出
2. 足球基础检测
3. 高活跃区域分析
4. 进球候选事件识别
5. 精彩片段候选识别
6. 向模块 B 和 D 输出结构化信号

不负责：

1. 球员号码识别
2. 个人集锦支持
3. 原始录像保存
4. 集锦导出
5. 平台管理页面

---

### 4.4 模块 D：事件与集锦生成模块

负责：

1. 读取录像文件
2. 读取视觉候选事件
3. 裁切事件视频片段
4. 生成全场精彩集锦
5. 导出集锦视频
6. 返回任务执行结果

不负责：

1. 视频接入
2. 视频推流
3. 主画面输出
4. 后台页面
5. 相机与流状态管理

---

### 4.5 模块 E：平台与调度模块

负责：

1. 比赛创建
2. 比赛开始与结束
3. 调度模块 A、B、C、D
4. 汇总状态
5. 后台页面
6. 主画面预览页面
7. 集锦任务触发与结果下载
8. 配置管理

不负责：

1. 视频编解码
2. 视觉推理
3. 集锦拼接
4. 录像实际写盘

---

## 5. 部署与网络拓扑约定

### 5.1 节点部署

| 节点 | 部署模块 |
|---|---|
| PC1 | A、B |
| PC2 | C |
| PC3 | D |
| PC4 | E |

### 5.2 网络要求

1. 所有 PC 必须在同一局域网内
2. 所有服务必须固定 IP 或固定主机名
3. 第 1 周末必须确定节点 IP、端口、共享目录路径
4. 第 2 周开始不得随意更改服务地址

### 5.3 推荐端口

| 服务 | 默认端口 |
|---|---|
| A 模块接口 | `8081` |
| B 模块接口 | `8082` |
| C 模块接口 | `8083` |
| D 模块接口 | `8084` |
| E 模块接口 | `8080` |
| RTSP 主机位流 | `8554` |
| RTSP 辅机位流 | `8555` |
| 主画面预览流 | `8560` |

---

## 6. 通信方式约定

### 6.1 通信分类

本项目共有三类通信。

#### 6.1.1 控制通信
用于开始比赛、停止比赛、创建任务、查询状态。  
通信方式统一为：

- HTTP REST
- JSON

#### 6.1.2 视频通信
用于相机流分发与视频消费。  
通信方式统一为：

- RTSP

#### 6.1.3 文件通信
用于录像文件、主画面录像和集锦导出。  
通信方式统一为：

- 共享目录
- 标准文件路径
- 平台通过 HTTP 返回文件索引与下载入口

---

## 7. 数据格式总约定

### 7.1 命名规则

所有 JSON 字段统一使用：

- 小写
- 下划线命名

示例：

- `match_id`
- `camera_id`
- `event_type`
- `start_sec`

### 7.2 时间字段规则

统一使用两类时间。

#### 7.2.1 绝对时间
- `timestamp_ms`
- 单位毫秒
- 用于视频帧或事件时间戳

#### 7.2.2 相对比赛时间
- `start_sec`
- `end_sec`
- 单位秒
- 用于视频裁切与集锦生成

### 7.3 通用响应格式

所有 HTTP 接口统一返回：

```json
{
  "code": 0,
  "message": "ok",
  "data": {}
}
```
--- 

### 7.4 通用错误码

| code | 含义      |
| ---- | ------- |
| 0    | 成功      |
| 1001 | 参数错误    |
| 1002 | 资源未初始化  |
| 1003 | 输入源不可用  |
| 1004 | 视频流异常   |
| 1005 | 文件写入失败  |
| 1006 | 文件读取失败  |
| 1007 | 任务执行失败  |
| 1008 | 上游服务不可达 |
| 1009 | 资源不存在   |
| 1010 | 状态冲突    |
| 1011 | 配置错误    |

### 7.5 通用状态值

统一使用：

- idle
- initializing
- running
- recording
- processing
- success
- failed
- stopped
8. 模块间接口契约
### 8.1 模块 E -> 模块 A
#### 8.1.1 接口：初始化采集任务

用途：通知模块 A 加载比赛上下文并初始化两路采集流。

方法：POST
路径：

/api/v1/ingest/matches/init

请求体：
```json
{
  "match_id": "match_20260405_001",
  "match_name": "school_match_demo",
  "camera_configs": [
    {
      "camera_id": "cam_01",
      "role": "main",
      "source_type": "rtsp",
      "source_uri": "rtsp://192.168.1.10/stream1"
    },
    {
      "camera_id": "cam_02",
      "role": "aux",
      "source_type": "rtsp",
      "source_uri": "rtsp://192.168.1.11/stream1"
    }
  ],
  "stream_output": {
    "main_stream_uri": "rtsp://192.168.1.101:8554/main",
    "aux_stream_uri": "rtsp://192.168.1.101:8555/aux",
    "codec": "h265",
    "resolution": "1920x1080",
    "fps": 25,
    "bitrate_kbps": 4000
  }
}
```
响应体：
```json
{
  "code": 0,
  "message": "ingest initialized",
  "data": {
    "match_id": "match_20260405_001",
    "status": "initializing"
  }
}
```
#### 8.1.2 接口：开始采集与推流

方法：POST
路径：

/api/v1/ingest/matches/{match_id}/start
```json
请求体：

{
  "operator": "platform-service"
}
```
#### 8.1.3 接口：停止采集与推流

方法：POST
路径：

/api/v1/ingest/matches/{match_id}/stop
8.1.4 接口：查询采集状态

方法：GET
路径：

/api/v1/ingest/matches/{match_id}/status

响应 data 示例：
```json
{
  "match_id": "match_20260405_001",
  "status": "running",
  "streams": [
    {
      "camera_id": "cam_01",
      "role": "main",
      "stream_uri": "rtsp://192.168.1.101:8554/main",
      "fps": 25,
      "bitrate_kbps": 3900,
      "status": "online"
    },
    {
      "camera_id": "cam_02",
      "role": "aux",
      "stream_uri": "rtsp://192.168.1.101:8555/aux",
      "fps": 25,
      "bitrate_kbps": 3800,
      "status": "online"
    }
  ]
}
```
8.2 模块 E -> 模块 B
8.2.1 接口：初始化录像与主画面任务

方法：POST
路径：

/api/v1/record/matches/init

请求体：
```json
{
  "match_id": "match_20260405_001",
  "input_streams": [
    {
      "camera_id": "cam_01",
      "role": "main",
      "stream_uri": "rtsp://192.168.1.101:8554/main"
    },
    {
      "camera_id": "cam_02",
      "role": "aux",
      "stream_uri": "rtsp://192.168.1.101:8555/aux"
    }
  ],
  "storage_config": {
    "raw_root": "/mnt/shared/raw",
    "program_root": "/mnt/shared/program"
  },
  "program_config": {
    "output_resolution": "1920x1080",
    "default_mode": "follow_focus_region"
  }
}
```
8.2.2 接口：开始录制与主画面输出

方法：POST
路径：

/api/v1/record/matches/{match_id}/start
8.2.3 接口：停止录制与主画面输出

方法：POST
路径：

/api/v1/record/matches/{match_id}/stop
8.2.4 接口：查询录像状态

方法：GET
路径：

/api/v1/record/matches/{match_id}/status

响应 data 示例：
```json
{
  "match_id": "match_20260405_001",
  "status": "recording",
  "raw_recording": true,
  "program_recording": true,
  "program_stream_uri": "rtsp://192.168.1.101:8560/program"
}
```
8.2.5 接口：查询文件索引

方法：GET
路径：

/api/v1/record/matches/{match_id}/files

响应 data 示例：
```json
{
  "match_id": "match_20260405_001",
  "raw_files": [
    {
      "camera_id": "cam_01",
      "file_path": "/mnt/shared/raw/match_20260405_001/cam_01.mp4"
    },
    {
      "camera_id": "cam_02",
      "file_path": "/mnt/shared/raw/match_20260405_001/cam_02.mp4"
    }
  ],
  "program_files": [
    {
      "file_path": "/mnt/shared/program/match_20260405_001/program.mp4"
    }
  ]
}
```
8.3 模块 C -> 模块 B
8.3.1 接口：更新主画面关注区域

用途：视觉模块将当前推荐关注区域发送给模块 B，用于生成自动转播主画面。

方法：POST
路径：

/api/v1/record/matches/{match_id}/focus-region

请求体：
```json
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
字段说明：

| 字段名               | 类型    | 约束/说明                                                                 |
|----------------------|---------|--------------------------------------------------------------------------|
| camera_id           | string  | 必须是主机位 cam_01                                                      |
| timestamp_ms        | integer | 本次关注区域对应的时间戳（毫秒）                                          |
| focus_region.x      | integer | 裁切区域左上角 x 坐标                                                     |
| focus_region.y      | integer | 裁切区域左上角 y 坐标                                                     |
| focus_region.width  | integer | 裁切宽度                                                                 |
| focus_region.height | integer | 裁切高度                                                                 |
| source_type         | string  | 数据来源。当前可用值：ball_position、motion_cluster、fallback_center       |
| confidence          | float   | 置信度，取值范围：0 ~ 1                                                   |

责任边界：

模块 C 负责提供关注区域建议
模块 B 负责最终裁切和平滑
模块 B 必须在没有关注区域时回退到默认中心区域
8.4 模块 E -> 模块 C
8.4.1 接口：初始化视觉分析任务

方法：POST
路径：

/api/v1/vision/matches/init

请求体：
```json
{
  "match_id": "match_20260405_001",
  "streams": [
    {
      "camera_id": "cam_01",
      "role": "main",
      "stream_uri": "rtsp://192.168.1.101:8554/main"
    },
    {
      "camera_id": "cam_02",
      "role": "aux",
      "stream_uri": "rtsp://192.168.1.101:8555/aux"
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
8.4.2 接口：开始视觉分析

方法：POST
路径：

/api/v1/vision/matches/{match_id}/start
8.4.3 接口：停止视觉分析

方法：POST
路径：

/api/v1/vision/matches/{match_id}/stop
8.4.4 接口：查询视觉状态

方法：GET
路径：

/api/v1/vision/matches/{match_id}/status
8.5 模块 C -> 模块 D
8.5.1 接口：事件候选数据提供接口

这里采用两层机制。

机制 1：模块 D 主动拉取

方法：GET
路径：

/api/v1/vision/matches/{match_id}/event-candidates

查询参数：

参数	类型	必填	说明
start_ms	integer	是	起始时间戳
end_ms	integer	是	结束时间戳

响应 data 示例：
```json
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
    },
    {
      "event_id": "evt_0002",
      "event_type": "danger_attack_candidate",
      "start_sec": 580.0,
      "end_sec": 586.5,
      "confidence": 0.78,
      "camera_id": "cam_01"
    }
  ]
}
```
机制 2：模块 C 本地持久化事件文件

可选输出文件：

/mnt/shared/metadata/{match_id}/event_candidates.json

此文件作为赛后恢复手段。
MVP 第一版建议实现 HTTP 拉取为主，事件文件为备份。

事件类型枚举

当前 v1 只允许以下类型：

goal_candidate
shot_candidate
danger_attack_candidate
celebration_candidate

任何新增事件类型必须先更新接口契约版本。

8.6 模块 E -> 模块 D
8.6.1 接口：初始化集锦任务上下文

方法：POST
路径：

/api/v1/highlight/matches/init

请求体：
```json
{
  "match_id": "match_20260405_001",
  "video_files": {
    "program_file": "/mnt/shared/program/match_20260405_001/program.mp4",
    "raw_files": [
      "/mnt/shared/raw/match_20260405_001/cam_01.mp4",
      "/mnt/shared/raw/match_20260405_001/cam_02.mp4"
    ]
  },
  "metadata_source": {
    "type": "http",
    "event_api": "http://192.168.1.102:8083/api/v1/vision/matches/match_20260405_001/event-candidates"
  }
}
```
8.6.2 接口：创建全场精彩集锦任务

方法：POST
路径：

/api/v1/highlight/matches/{match_id}/full-highlight

请求体：
```json
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
8.6.3 接口：查询集锦任务状态

方法：GET
路径：

/api/v1/highlight/tasks/{task_id}

响应 data 示例：
```json
{
  "task_id": "task_highlight_001",
  "match_id": "match_20260405_001",
  "status": "success",
  "output_file": "/mnt/shared/output/match_20260405_001/full_highlight.mp4",
  "event_count": 6
}
```
8.7 模块 D -> 模块 E
8.7.1 接口返回要求

模块 D 必须返回：

任务状态
输出文件路径
参与拼接的事件数量
出错时的错误码与错误信息

平台模块只负责展示和下载，不负责实际拼接。

### 9. 文件路径契约

所有节点必须挂载统一共享目录，至少包括：

/mnt/shared/raw
/mnt/shared/program
/mnt/shared/output
/mnt/shared/metadata
/mnt/shared/logs
#### 9.1 文件命名规则
#### 9.1.1 原始录像
/mnt/shared/raw/{match_id}/cam_01.mp4
/mnt/shared/raw/{match_id}/cam_02.mp4
#### 9.1.2 主画面录像
/mnt/shared/program/{match_id}/program.mp4
#### 9.1.3 集锦输出
/mnt/shared/output/{match_id}/full_highlight.mp4
#### 9.1.4 事件候选元数据
/mnt/shared/metadata/{match_id}/event_candidates.json
### 10. 日志契约

每个模块日志必须至少包含以下字段：

timestamp
module_name
level
match_id
task_id
message
#### 10.1 日志级别

统一使用：

DEBUG
INFO
WARN
ERROR
10.2 日志责任
各模块维护本模块详细日志
模块 E 只汇总关键状态和错误摘要
不允许 silent fail
任何任务失败必须写入 ERROR 日志并返回错误码
#### 11. 联调顺序约定

联调必须按以下顺序进行，不允许跳步。

#### 11.1 第一步：A 模块单独联通

目标：

两路视频可接入
RTSP 流可被拉取
11.2 第二步：A + B 联调

目标：

两路录像可保存
主画面可输出
#### 11.3 第三步：A + C 联调

目标：

视觉模块可消费 RTSP 流
可输出关注区域和候选事件
11.4 第四步：B + C 联调

目标：

主画面可接收关注区域进行裁切
#### 11.5 第五步：C + D 联调

目标：

集锦模块可获取候选事件
可根据时间窗裁切片段
#### 11.6 第六步：E 接入所有模块

目标：

平台可控制比赛生命周期
平台可查看状态
平台可触发集锦生成
### 12. 变更控制规则
12.1 接口冻结规则
第 1 周结束前冻结 v1
第 2 周到第 6 周原则上不允许改字段语义
第 4 周后禁止新增跨模块关键字段
第 6 周后只允许修 bug，不允许大改接口
#### 2.2 允许的变更

允许：

新增可选字段
补充日志字段
修复拼写错误
新增不影响旧逻辑的状态信息
#### 12.3 不允许的变更

不允许：

修改已有字段名称
改变字段含义
改变路径语义
改动核心事件类型枚举
把 HTTP 改成其他控制协议而不经团队评审
### 13. 责任归属规则
#### 13.1 A 模块责任边界

如果 RTSP 流不可拉取、码率异常、流中断，责任优先归 A。

#### 13.2 B 模块责任边界

如果录像文件缺失、主画面不生成、裁切异常，责任优先归 B。

#### 13.3 C 模块责任边界

如果关注区域不输出、事件候选为空、事件数据格式错误，责任优先归 C。

#### 13.4 D 模块责任边界

如果有录像和事件输入但集锦不生成、输出文件损坏，责任优先归 D。

#### 13.5 E 模块责任边界

如果后台无法调度、状态不显示、任务无法触发、下载入口无效，责任优先归 E。

#### 14. Mermaid 通信链路图

```flowchart LR
    subgraph PC1["PC1"]
        A["A: ingest-streaming-module"]
        B["B: record-program-module"]
    end

    subgraph PC2["PC2"]
        C["C: vision-event-module"]
    end

    subgraph PC3["PC3"]
        D["D: highlight-generation-module"]
    end

    subgraph PC4["PC4"]
        E["E: platform-orchestration-module"]
    end

    CAM1["Camera 1"]
    CAM2["Camera 2"]

    SHARED["Shared Storage\n/mnt/shared"]

    CAM1 --> A
    CAM2 --> A

    A -- "RTSP main/aux" --> B
    A -- "RTSP main/aux" --> C

    C -- "focus_region JSON" --> B
    C -- "event_candidates HTTP/JSON" --> D

    B -- "raw/program files" --> SHARED
    D -- "read raw/program files" --> SHARED
    D -- "highlight output" --> SHARED

    E -- "HTTP control" --> A
    E -- "HTTP control" --> B
    E -- "HTTP control" --> C
    E -- "HTTP control" --> D

    E -- "query/download" --> SHARED