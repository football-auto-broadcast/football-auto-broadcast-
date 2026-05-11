\# 录像文件命名与归档规范 v1

\*\*模块：\*\* B (record-program-module)

\*\*最后更新：\*\* 2026-04-19



\## 1. 存储路径映射契约

为适配 Windows Docker 容器化部署与单机集中式架构，统一约定以下路径映射关系：

\*   \*\*物理宿主机路径 (Host)\*\*: `V:\\Football\_Storage` (RTX 5070 所在主机的独立数据盘)

\*   \*\*容器内挂载路径 (Container)\*\*: `C:\\mnt\\shared` (代码中硬编码的根目录)



\## 2. 目录树结构

模块 B 启动时将自动在 `C:\\mnt\\shared` 下创建并管理以下结构：

```text

C:\\mnt\\shared\\

├── raw\\                  # 原始 4K 录像目录

│   └── {match\_id}\\       # 按比赛 ID 分区

├── program\\              # 主转播画面录像目录

│   └── {match\_id}\\       

└── logs\\                 # 运行日志

3\. 文件命名规范与编码

所有落盘视频统一采用 MP4 封装 + H.264 编码 (四字符码: avc1)。

主机位原始流: C:\\mnt\\shared\\raw\\{match\_id}\\cam\_01.mp4

辅机位原始流: C:\\mnt\\shared\\raw\\{match\_id}\\cam\_02.mp4

1080P 裁切主画面: C:\\mnt\\shared\\program\\{match\_id}\\program.mp4

4\. 容灾与权限自检

服务启动（Init 接口被调用）时，模块 B 必须测试对应 match\_id 目录的读写权限。若写入失败，需立即抛出 HTTP 500 错误码（对应契约错误码 1005: 文件写入失败），拒绝录制。



