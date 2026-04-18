# football-auto-broadcast-足球赛事自动转播&精彩集锦生成系统
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/github/actions/workflow/status/your-org/football-auto-broadcast/build.yml?branch=main&label=build)  
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/github/actions/workflow/status/your-org/football-auto-broadcast/test.yml?branch=main&label=tests)  
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/github/license/your-org/football-auto-broadcast)  
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/badge/C%2B%2B-17-blue)  
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/badge/status-MVP-orange)  
<!-- 这是一张图片，ocr 内容为： -->
![](https://img.shields.io/badge/architecture-single--host-green)

一个面向校队比赛与训练赛的轻量、可移动部署的足球自动转播与精彩集锦生成系统。

本项目当前聚焦于一个可落地的 MVP 版本，采用 **双 GigE 工业相机 + 千兆 PoE 交换机 + 单主机集中式处理架构**。系统由一台主机统一完成采集、录制、自动转播主画面生成、事件识别和赛后全场精彩集锦生成。

---

## 项目简介
足球比赛的视频采集与精彩集锦制作通常依赖大量人工，流程繁琐、耗时较长。  
本项目旨在构建一套低成本、可移动部署的足球赛事自动转播系统，支持以下能力：

+ 接入两路工业相机视频
+ 全场原始视频录制
+ 实时输出基础自动转播主画面
+ 识别进球和精彩片段候选事件
+ 赛后自动生成全场精彩集锦
+ 提供比赛控制、状态查看和结果下载后台

本系统适用于校队比赛、训练赛、校园杯等中小型足球场景。

---

## MVP 范围
第一版 MVP 聚焦于打通一条稳定的端到端闭环流程。

### MVP 已包含功能
+ 两路工业相机视频接入
+ 原始视频实时录制
+ 基于主机位的基础自动转播主画面生成
+ 进球候选事件识别
+ 精彩片段候选识别
+ 赛后自动生成全场精彩集锦
+ 基于 Git 的团队协作开发
+ 单主机集中式部署

### 未来迭代功能
+ 自研无线图传
+ 复杂多机位自动切镜
+ 球员号码识别
+ 个人集锦生成
+ 全自动跨镜头球员身份统一
+ 战术分析与高级统计
+ 手机 App
+ 公网直播与云端部署

---

## 硬件方案
当前项目硬件方案如下：

+ 工业相机：海康 GigE 工业相机 2 台
+ 镜头：6mm 或 8mm 广角镜头 2 只
+ 交换机：千兆 PoE 交换机 1 台
+ 主机：RTX 4080 主机 1 台
+ 网线：Cat6 六类网线若干
+ 支架与防护：根据场地实际部署补充

---

## 系统架构
系统采用 **单主机集中式处理架构**。

<!-- 这是一个文本绘图，源码为：flowchart LR

    CAM1["工业相机1\nGigE\n主机位"]
    CAM2["工业相机2\nGigE\n辅机位"]

    SW["千兆 PoE 交换机"]

    HOST["主机 PC\nRTX 4080\n统一采集 / 录制 / 分析 / 集锦生成"]

    VIEW["可选控制端\n笔记本或普通 PC\n用于网页控制与结果查看"]

    CAM1 --> SW
    CAM2 --> SW
    SW --> HOST
    HOST --> VIEW -->
![](https://cdn.nlark.com/yuque/__mermaid_v3/15da9b57663a5a072f103ebb1dda2811.svg)

## 通信架构
<!-- 这是一个文本绘图，源码为：flowchart TB

    subgraph L1["硬件层"]
        CAM1["工业相机1<br/>GigE / 主机位"]
        CAM2["工业相机2<br/>GigE / 辅机位"]
        SW["千兆 PoE 交换机"]
    end
    
    subgraph L2["接入层"]
        A["ingest-streaming-module<br/>采集与编码分发"]
    end
    
    subgraph L3["处理层"]
        B["record-program-module<br/>录像与主画面生成"]
        C["vision-event-module<br/>视觉分析"]
        D["highlight-generation-module<br/>事件与集锦生成"]
    end
    
    subgraph L4["存储层"]
        RAW["raw<br/>原始录像"]
        PROGRAM["program<br/>主画面录像"]
        META["metadata<br/>候选事件数据"]
        OUTPUT["output<br/>集锦输出"]
        LOGS["logs<br/>运行日志"]
    end
    
    subgraph L5["平台层"]
        E["platform-orchestration-module<br/>平台与调度"]
        VIEW["浏览器 / 可选控制端"]
    end
    
    CAM1 --> SW
    CAM2 --> SW
    SW --> A
    
    A --> B
    A --> C
    
    C -- "关注区域" --> B
    C -- "候选事件" --> D
    
    B --> RAW
    B --> PROGRAM
    C --> META
    D --> OUTPUT
    
    A --> LOGS
    B --> LOGS
    C --> LOGS
    D --> LOGS
    E --> LOGS
    
    RAW --> D
    PROGRAM --> D
    META --> D
    
    E -- "HTTP 控制" --> A
    E -- "HTTP 控制" --> B
    E -- "HTTP 控制" --> C
    E -- "HTTP 控制" --> D
    
    B -- "主画面预览" --> E
    D -- "任务状态 / 输出路径" --> E
    
    VIEW --> E -->

![](https://cdn.nlark.com/yuque/__mermaid_v3/f4a431362e0be3e464a45c50791ca64b.svg)

--- 

### 核心思路
+ 两台工业相机通过 GigE 接入千兆 PoE 交换机
+ 主机通过工业相机 SDK 统一取流
+ 主机统一完成录制、主画面生成、视觉分析、候选事件识别与集锦生成
+ 可选控制端通过浏览器访问后台页面，完成比赛控制与结果查看

---

## 部署拓扑
+ **工业相机 1**
    - 主机位
    - 全场覆盖
+ **工业相机 2**
    - 辅机位
    - 补充画面与素材，辅助识别
+ **主机 PC**
    - 工业相机取流
    - 原始录像
    - 自动转播主画面
    - 视觉分析
    - 集锦生成
    - 平台服务
+ **可选控制端**
    - 访问后台页面
    - 查看主画面预览
    - 查看集锦结果
    - 下载输出文件

---

## **主要流程**
### **比赛前**
1. 架设两台工业相机
2. 连接千兆 PoE 交换机
3. 主机加载工业相机 SDK 并初始化取流
4. 在平台中创建比赛任务

### **比赛中**
1. 开始比赛录制
2. 录制两路原始视频
3. 输出一路自动转播主画面
4. 视觉模块持续生成关注区域和候选事件数据

### **比赛后**
1. 结束比赛录制
2. 读取录像文件与候选事件
3. 生成全场精彩集锦
4. 在平台中查看并下载集锦结果

---

## **技术栈**
### **核心语言**
+ C++
+ Python，用于训练脚本和离线工具

### **多媒体处理**
+ GStreamer
+ FFmpeg
+ OpenCV

### **AI 推理**
+ ONNX Runtime
+ TensorRT

### **工业相机接入**
+ 相机厂商 SDK
+ GigE 取流
+ 本地编码与文件写入

### **协作方式**
+ Git
+ Pull Request 工作流

---

## **逻辑模块划分**
虽然运行架构已经改为单主机集中式处理，但工程实现上仍然保留模块化设计，便于多人协作与后续扩展。

### **ingest-streaming-module**
+ 工业相机接入
+ 工业相机 SDK 取流
+ 视频编码压缩
+ 视频流分发
+ 流状态上报

### **record-program-module**
+ 原始录像保存
+ 自动转播主画面生成
+ 主画面录制
+ 文件归档与索引

### **vision-event-module**
+ 足球基础检测
+ 高活跃区域分析
+ 主画面关注区域输出
+ 进球候选识别
+ 精彩片段候选识别

### **highlight-generation-module**
+ 候选事件解析
+ 视频片段裁切
+ 全场精彩集锦生成
+ 集锦导出

### **platform-orchestration-module**
+ 比赛生命周期管理
+ 各模块任务编排
+ 任务状态跟踪
+ 后台管理页面
+ 主画面预览
+ 集锦结果下载
+ 配置与日志管理

---

## **项目特点**
+ **低成本可落地**  
采用双工业相机 + 单主机集中式处理，降低系统联调复杂度。
+ **关注 MVP 可落地性**  
聚焦自动转播主画面和全场精彩集锦两条主线，控制项目复杂度。
+ **工业相机适配**  
系统针对 GigE 工业相机链路设计，适合固定机位和稳定采集场景。
+ **适合 RTX 4080 主机**  
主机性能足以承担双路采集、录像、主画面生成、视觉分析和集锦输出。
+ **模块职责清晰**  
逻辑模块边界明确，便于多人并行开发与后续扩展。

---

## **当前状态**
当前仓库处于 MVP 设计与实现阶段。

计划中的首个公开里程碑包括：

+ 双工业相机视频接入
+ 自动转播主画面输出
+ 进球与精彩片段候选识别
+ 赛后全场精彩集锦生成
+ 平台统一控制与结果下载

---

## **适用场景**
本项目当前最适合以下场景：

+ 校队训练赛
+ 校园足球比赛
+ 固定机位赛事记录
+ 赛后精彩集锦快速生成

当前阶段不面向：

+ 商业级大型直播平台
+ 高并发公网直播
+ 多机位复杂导播系统
+ 球员身份精细分析系统

---

## **联系方式**
如有问题、功能建议或合作需求，请通过 Issue 或 Pull Request 联系。

