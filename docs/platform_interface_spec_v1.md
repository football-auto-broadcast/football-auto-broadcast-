# 平台调度接口说明 v1

## 1. 概述
本文档定义平台与调度模块（E）需要调用的其他模块（A/B/C/D）的 HTTP 接口，
以及平台自身向前端提供的 API。

所有接口遵循 `127.0.0.1` + 各自端口，统一 JSON 格式： `{"code":0,"message":"ok","data":{}}`。

---

## 2. 平台需要调用的下游模块接口

### 2.1 采集模块 A (8081)
| 操作 | 方法 | 路径 |
|------|------|------|
| 初始化采集 | POST | `/api/v1/ingest/matches/init` |
| 开始采集 | POST | `/api/v1/ingest/matches/{match_id}/start` |
| 停止采集 | POST | `/api/v1/ingest/matches/{match_id}/stop` |
| 查询状态 | GET  | `/api/v1/ingest/matches/{match_id}/status` |

### 2.2 录像模块 B (8082)
| 操作 | 方法 | 路径 |
|------|------|------|
| 初始化录制 | POST | `/api/v1/record/matches/init` |
| 开始录制 | POST | `/api/v1/record/matches/{match_id}/start` |
| 停止录制 | POST | `/api/v1/record/matches/{match_id}/stop` |
| 查询状态 | GET  | `/api/v1/record/matches/{match_id}/status` |
| 查询文件索引 | GET  | `/api/v1/record/matches/{match_id}/files` |

### 2.3 视觉分析模块 C (8083)
| 操作 | 方法 | 路径 |
|------|------|------|
| 初始化视觉 | POST | `/api/v1/vision/matches/init` |
| 开始分析 | POST | `/api/v1/vision/matches/{match_id}/start` |
| 停止分析 | POST | `/api/v1/vision/matches/{match_id}/stop` |
| 查询状态 | GET  | `/api/v1/vision/matches/{match_id}/status` |
| 查询候选事件 | GET  | `/api/v1/vision/matches/{match_id}/event-candidates` |

### 2.4 集锦生成模块 D (8084)
| 操作 | 方法 | 路径 |
|------|------|------|
| 初始化集锦上下文 | POST | `/api/v1/highlight/matches/init` |
| 生成全场集锦 | POST | `/api/v1/highlight/matches/{match_id}/full-highlight` |
| 查询任务状态 | GET  | `/api/v1/highlight/tasks/{task_id}` |

---

## 3. 平台向前端提供的管理 API
（已实现部分）

| 操作 | 方法 | 路径 | 说明 |
|------|------|------|------|
| 健康检查 | GET  | `/api/v1/health` | 返回 `{"status":"ok"}` |
| 创建比赛 | POST | `/api/v1/matches` | 请求体 `{"match_name":"..."}` |
| 比赛列表 | GET  | `/api/v1/matches` | 返回所有比赛 |
| 单个比赛 | GET  | `/api/v1/matches/:match_id` | 比赛详情 |
| 系统状态 | GET  | `/api/v1/system/status` | 汇聚各模块在线状态 |

（未来将增加 `start/stop/highlight` 接口，对应调度操作）

---

## 4. 配置约定
- 各模块端口从 `configs/config.ini` 读取，默认值如上述。
- 所有模块通信均使用 `http://127.0.0.1:{port}`。