\# 模块 B：录像与主画面生成模块 (Final 封版说明)



\## 1. 模块能力清单 (已验收)

\- \[x] \*\*五路视频并发处理\*\*：支持同时操作 2 路 4K 输入、2 路 1080P 裁切、1 路主画面流。

\- \[x] \*\*GPU 硬件加速直播\*\*：集成 FFmpeg 管道，调用 RTX 5070 NVENC 芯片实现极低延迟 RTSP 推流。

\- \[x] \*\*配置热插拔\*\*：所有路径、端口、算法参数抽离至 `config.json`，支持零编译改配。

\- \[x] \*\*多重容错防线\*\*：自带 AI 坐标掉线回退（1s）、导播决策掉线回退（3s）机制。

\- \[x] \*\*标准化资产交接\*\*：任务结束自动生成 `record\_index.json` 供集锦模块提取。



\## 2. 部署与启动方式

1\. 确保已安装并启动 `MediaMTX`（配置 8560 端口）。

2\. 将程序 `record\_program\_service.exe` 置于根目录，旁放 `config.json`。

3\. 直接双击运行，控制台出现 `\[PRO] Module B Live at 8082` 即代表启动成功。



\## 3. 核心 API 清单 (端口: 8082)

\* `POST /api/v1/record/matches/init` (初始化比赛)

\* `POST /api/v1/record/matches/:id/start` (开始录制并开启推流)

\* `POST /api/v1/record/matches/:id/focus-regions` (接收 AI 坐标，驱动 EMA 镜头平滑)

\* `POST /api/v1/record/matches/:id/program-decision` (接收导播指令，切换主视角)

\* `POST /api/v1/record/matches/:id/stop` (停止录制，生成索引)



\## 4. 直播预览地址

\* 网络串流播放器 (如 VLC) 访问：`rtsp://127.0.0.1:8560/program`

