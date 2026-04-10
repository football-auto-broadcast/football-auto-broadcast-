# Communication Standard v1
足球赛事自动转播系统通信标准文档（工程版）

## 目录
1. 文档信息  
2. 文档定位  
3. 通信分层标准  
4. 工程总原则  
5. HTTP 通信工程规范  
6. JSON 消息工程规范  
7. 通用消息头与请求头规范  
8. 通用响应体规范  
9. 字段字典  
10. 核心消息 Schema  
11. RTSP 视频通信工程规范  
12. 文件通信工程规范  
13. 幂等性、重试与超时规范  
14. 版本管理与兼容性规范  
15. 错误处理与日志关联规范  
16. 联调与验收校验规范  
17. 最终结论  

---

## 1. 文档信息

- 文档名称：Communication Standard  
- 版本：v1.0  
- 文档类型：工程通信标准  
- 适用阶段：MVP 第一版  
- 适用项目：足球赛事自动转播系统  
- 上游依据：Team Interface Contract v1  

---

## 2. 文档定位

本文件不是接口列表文档，也不是业务需求文档。  

本文件专门回答这几个工程问题：

- 模块之间到底用什么协议通信  
- JSON 该怎么写、字段该怎么命名  
- 请求失败怎么处理  
- 路径、时间、状态、错误码怎么统一  
- 联调时应该校验什么  
- 后续小改版时怎样兼容  

---

## 3. 通信分层标准

本项目通信分为 4 层。

### 3.1 控制层

用于启动、停止、初始化、创建任务、查询状态。  

标准：

- HTTP REST  
- JSON  

### 3.2 结构化数据层

用于关注区域、候选事件、文件索引、任务结果。  

标准：

- HTTP REST  
- JSON  

### 3.3 媒体流层

用于视频流传输。  

标准：

- RTSP  

### 3.4 文件交换层

用于录像、元数据文件、集锦输出、日志。  

标准：

- 共享目录  
- 标准路径  

这个分层与接口约定文档完全一致。  

---

## 4. 工程总原则

### 4.1 协议职责单一

- HTTP 只传控制和结构化数据  
- RTSP 只传视频  
- 共享目录只传文件  

### 4.2 配置优先

所有 IP、端口、路径、流地址都必须可配置。  

禁止写死：

- IP  
- 共享盘符  
- Linux 路径  
- Windows 路径  

### 4.3 跨平台透明

Windows 和 Linux 模块都必须只依赖：

- HTTP  
- JSON  
- RTSP  
- 配置化路径  

### 4.4 日志可追踪

每个请求、任务、输出文件都要能在日志里追到。  

### 4.5 失败可恢复

任务失败、上游不可用、文件缺失时，必须有明确错误信息，不能沉默失败。  

---

## 5. HTTP 通信工程规范

### 5.1 协议要求

统一使用：

- HTTP/1.1  
- REST 风格  
- UTF-8 JSON  

### 5.2 方法约定

| 方法 | 语义 |
|------|------|
| GET  | 查询类接口 |
| POST | 创建、初始化、启动、停止、更新类接口 |

MVP 阶段不强制使用 PUT、PATCH、DELETE。  

### 5.3 URL 设计规范

统一风格：


### 5.4 查询参数规范

查询类参数统一放 URL query 中，例如：

- start_ms  
- end_ms  
- limit  

禁止把纯查询参数放入 POST Body。  

---

## 6. JSON 消息工程规范

### 6.1 编码要求

- UTF-8  
- 顶层必须是 JSON object  
- 不允许 BOM  
- 不允许非标准注释  

### 6.2 命名要求

统一使用 snake_case，例如：

- match_id  
- camera_id  
- stream_uri  
- event_type  

### 6.3 值类型要求

必须严格区分：

- integer  
- number  
- string  
- boolean  
- array  
- object  

禁止拿字符串代替数值。

错误示例：

```json
{
  "fps": "25"
}
```
### 6.4 空值规范

- 不推荐返回 `null`
- 可选字段缺失优先
- 必填字段禁止缺失

## 7. 通用消息头与请求头规范

### 7.1 请求头

所有 JSON 请求必须带：

```http
Content-Type: application/json
Accept: application/json
```

### 7.2 推荐增加的请求头
X-Request-Id: req_20260405_0001
X-Match-Id: match_20260405_001
X-Caller-Module: platform-orchestration-module
Header	用途
X-Request-Id	请求链路追踪
X-Match-Id	比赛上下文
X-Caller-Module	调用方模块名
### 7.3 响应头

推荐返回：

Content-Type: application/json; charset=utf-8
X-Request-Id: req_20260405_0001
## 8. 通用响应体规范

统一格式：
```json
{
  "code": 0,
  "message": "ok",
  "data": {}
}
```

### 8.1 字段要求
字段	类型	必填	说明
code	integer	是	错误码或成功码
message	string	是	简要说明
data	object	是	业务数据
### 8.2 成功响应示例
```json
{
  "code": 0,
  "message": "recording started",
  "data": {
    "match_id": "match_20260405_001",
    "status": "recording"
  }
}
```
### 8.3 失败响应示例
```json
{
  "code": 1008,
  "message": "vision service unavailable",
  "data": {
    "upstream": "vision-event-module"
  }
}
```
## 9. 字段字典
### 9.1 基础上下文字段
字段	类型	说明
match_id	string	比赛唯一标识
match_name	string	比赛名称
camera_id	string	相机唯一标识
role	string	main 或 aux
task_id	string	任务唯一标识
module_name	string	模块名
### 9.2 时间字段
字段	类型	单位	说明
timestamp_ms	integer	毫秒	绝对时间
start_sec	number	秒	相对比赛时间起点
end_sec	number	秒	相对比赛时间终点
### 9.3 视频字段
字段	类型	说明
stream_uri	string	RTSP 地址
fps	integer	帧率
bitrate_kbps	integer	码率
resolution	string	分辨率，例如 1920x1080
### 9.4 关注区域字段
字段	类型	说明
focus_region	object	推荐裁切区域
x	integer	左上角 x
y	integer	左上角 y
width	integer	宽度
height	integer	高度
source_type	string	来源类型
confidence	number	0 到 1
### 9.5 事件字段
字段	类型	说明
event_id	string	事件唯一标识
event_type	string	事件类型
confidence	number	置信度
camera_id	string	来源相机
### 9.6 文件字段
字段	类型	说明
file_path	string	文件完整路径
raw_files	array	原始录像文件列表
program_files	array	主画面录像列表
output_file	string	集锦输出文件路径
## 10. 核心消息 Schema
### 10.1 MatchInitRequest
```json
{
  "match_id": "match_20260405_001",
  "match_name": "school_match_demo"
}
```
必填字段：

match_id
match_name
### 10.2 CameraConfig
```json
{
  "camera_id": "cam_01",
  "role": "main",
  "source_type": "rtsp",
  "source_uri": "rtsp://192.168.1.10/stream1"
}
```

必填字段：

camera_id
role
source_type
source_uri
### 10.3 StreamDescriptor
```json
{
  "camera_id": "cam_01",
  "role": "main",
  "stream_uri": "rtsp://192.168.1.101:8554/main",
  "fps": 25,
  "bitrate_kbps": 3900,
  "status": "online"
}
```
### 10.4 FocusRegionMessage
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
校验规则：

camera_id 必须为 cam_01
confidence 范围在 [0,1]
width、height 必须大于 0
### 10.5 EventCandidateMessage
```json
{
  "event_id": "evt_0001",
  "event_type": "goal_candidate",
  "start_sec": 312.4,
  "end_sec": 320.6,
  "confidence": 0.92,
  "camera_id": "cam_01"
}
```
校验规则：

event_type 必须属于枚举
end_sec 必须大于 start_sec
confidence 范围在 [0,1]

事件类型枚举：

goal_candidate
shot_candidate
danger_attack_candidate
celebration_candidate
### 10.6 FileIndexMessage
```json
{
  "match_id": "match_20260405_001",
  "raw_files": [
    {
      "camera_id": "cam_01",
      "file_path": "Z:\\raw\\match_20260405_001\\cam_01.mp4"
    }
  ],
  "program_files": [
    {
      "file_path": "Z:\\program\\match_20260405_001\\program.mp4"
    }
  ]
}
```
### 10.7 HighlightTaskResultMessage
```json
{
  "task_id": "task_highlight_001",
  "match_id": "match_20260405_001",
  "status": "success",
  "output_file": "Z:\\output\\match_20260405_001\\full_highlight.mp4",
  "event_count": 6
}
```
## 11. RTSP 视频通信工程规范
### 11.1 流命名规范
rtsp://{pc1_ip}:8554/main
rtsp://{pc1_ip}:8555/aux
rtsp://{pc1_ip}:8560/program
### 11.2 使用规范
main 表示主机位
aux 表示辅机位
program 表示主画面预览流
### 11.3 工程要求
RTSP 地址必须可配置
拉流失败必须记录重连日志
拉流失败不能导致服务退出
## 12. 文件通信工程规范
### 12.1 共享目录逻辑
{shared_root}/raw
{shared_root}/program
{shared_root}/output
{shared_root}/metadata
{shared_root}/logs
### 12.2 平台无关原则

Windows：

Z:\raw
Z:\program
Z:\output
Z:\metadata
Z:\logs

Linux：

/mnt/shared/raw
/mnt/shared/program
/mnt/shared/output
/mnt/shared/metadata
/mnt/shared/logs
### 12.3 文件可见性要求
B 写入录像，D 必须可读
C 写入事件文件，D 必须可读
D 写入结果，E 必须可读
12.4 文件落盘完成判定
写入结束后再写索引
输出前关闭句柄
避免读取未写完文件
## 13. 幂等性、重试与超时规范
### 13.1 幂等性要求

以下接口必须幂等：

初始化
开始
停止
状态查询
### 13.2 重试规则
接口类型	重试次数	间隔
状态查询	2	2 秒
初始化	2	2 秒
开始/停止	2	2 秒
集锦任务创建	1	3 秒
### 13.3 超时规则
接口类型	超时
状态查询	3 秒
初始化	10 秒
开始/停止	10 秒
集锦任务创建	10 秒
### 14. 版本管理与兼容性规范
14.1 版本冻结
第 1 周冻结 v1
第 2 周按 v1 开发
修改升级 v1.1
### 14.2 向后兼容规则

允许：
新增可选字段
新增状态

不允许：
删除必填字段
修改字段语义
修改枚举含义
### 14.3 文档优先级

通信文档优先于代码。

## 15. 错误处理与日志关联规范
### 15.1 请求可追踪

必须支持：

X-Request-Id
match_id
task_id
15.2 日志字段
timestamp
module_name
level
match_id
task_id
request_id
message
15.3 ERROR 条件
上游接口不可达
RTSP 拉流失败
文件读写失败
JSON 解析失败
数据非法
集锦失败
## 16. 联调与验收校验规范
### 16.1 JSON 校验
字段命名
时间单位
code / message / data 完整
confidence 范围
event_type 枚举
### 16.2 RTSP 校验
主流可播放
辅流可播放
program 流可播放
日志正常
### 16.3 文件链路校验
原始录像存在
主画面存在
元数据存在
输出存在
可读取
### 16.4 跨平台校验
Windows 调 Linux 正常
Linux 调 Windows 正常
共享目录权限正常
路径未写死