# 模块 D 样例输入事件文件说明书 (基于 C 模块 v1.1 规约)

**文档版本**: v1.0  
**最后更新**: 2026-05-20  
**适用路径**: `configs/highlight-generation-module/events.json`  
**测试数据状态**: 生产环境级全真模拟桩数据

---

## 1. 文档目的

本文档提供一份完全符合黄俊成（C 模块）`事件 JSON 格式说明 v1.1` 校验规则的 `events.json` 落地样例文件。本文件模拟了一场真实足球赛事的视觉分析结果，其中包含高光交织、时间轴重叠、置信度波动的多路机位原始候选事件流。该文件作为模块 D 在第一周至第五周进行离线测试桩（Stub）调试的标准输入源。

---

## 2. 样例数据完整 JSON 源码
请直接复制以下 JSON 代码块，并在本地创建为 `configs/highlight-generation-module/events.json` 文件：

```json
[
  {
    "event_id": "evt_0001",
    "event_type": "shot_candidate",
    "start_sec": 120.5,
    "end_sec": 132.0,
    "confidence": 0.78,
    "camera_id": "cam_01"
  },
  {
    "event_id": "evt_0002",
    "event_type": "danger_attack_candidate",
    "start_sec": 118.0,
    "end_sec": 130.0,
    "confidence": 0.85,
    "camera_id": "cam_02"
  },
  {
    "event_id": "evt_0003",
    "event_type": "goal_candidate",
    "start_sec": 315.0,
    "end_sec": 328.5,
    "confidence": 0.94,
    "camera_id": "cam_01"
  },
  {
    "event_id": "evt_0004",
    "event_type": "celebration_candidate",
    "start_sec": 325.0,
    "end_sec": 350.2,
    "confidence": 0.88,
    "camera_id": "cam_01"
  },
  {
    "event_id": "evt_0005",
    "event_type": "shot_candidate",
    "start_sec": 1420.0,
    "end_sec": 1431.5,
    "confidence": 0.62,
    "camera_id": "cam_02"
  },
  {
    "event_id": "evt_0006",
    "event_type": "danger_attack_candidate",
    "start_sec": 2100.0,
    "end_sec": 2115.0,
    "confidence": 0.81,
    "camera_id": "cam_01"
  }
]
```

## 3. 业务流测例深度解析 (联调测试重点)

本桩数据精心构造了以下三个边界验证场景，专门用于验证模块 D 的《事件筛选与排序规则》状态机：

### 3.1 场景一：时域重叠去重与多机位融合（`evt_0001` 与 `evt_0002`）

- **现象描述**：在比赛第 118 秒到 132 秒之间，主机位（`cam_01`）上报了射门候选，而副机位（`cam_02`）几乎在同一时间段内由于运动聚类上报了危险进攻。
- **模块 D 预期行为**：检测到交叉重叠（$\max(120.5, 118.0) \le \min(132.0, 130.0)$）。由于两个事件类型不同，且射门（`shot_candidate`）的基础权重高于危险进攻，算法需要执行异类关联吞噬，或进行时间轴并集裁剪，防止一段视频在最终成片里被切两次。

### 3.2 场景二：进球与庆祝智能无缝衔接（`evt_0003` 与 `evt_0004`）

- **现象描述**：第 315 秒爆发进球，在进球尚未结束的第 325 秒，主机位捕捉到了长达 25.2 秒的庆祝动作。
- **模块 D 预期行为**：命中“进球与庆祝融合”规则（庆祝的 `start_sec` 处于进球的 `end_sec` 缓冲带内）。模块 D 必须自动将进球的裁剪尾部向后延伸，更新终点为 `350.2` 秒，将两段逻辑视频无缝融合成一个单体高光切片。

### 3.3 场景三：低置信度噪声剔除（`evt_0005`）

- **现象描述**：第 1420 秒发生一次射门候选，但置信度仅为 `0.62`。
- **模块 D 预期行为**：由于该事件置信度低于射门门槛阈值（`0.75`），模块 D 内部清洗引擎必须直接丢弃该事件，不为其分配任何 FFmpeg 裁剪句柄。