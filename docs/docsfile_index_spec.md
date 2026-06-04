\# 文件索引结构说明 v1

\*\*模块：\*\* B (record-program-module)

\*\*最后更新：\*\* 2026-04-19



\## 1. 结构定义

本结构用于响应 `GET /api/v1/record/matches/{match\_id}/files` 接口，为下游模块（如模块 D 集锦生成）提供视频文件的物理/共享绝对路径。



\## 2. JSON 结构规范

```json

{

&#x20; "code": 0,

&#x20; "message": "ok",

&#x20; "data": {

&#x20;   "match\_id": "string (比赛唯一标识)",

&#x20;   "raw\_files":\[

&#x20;     {

&#x20;       "camera\_id": "cam\_01 (机位标识)",

&#x20;       "file\_path": "string (本地或共享绝对路径)"

&#x20;     }

&#x20;   ],

&#x20;   "program\_files":\[

&#x20;     {

&#x20;       "file\_path": "string (裁切后的成片绝对路径)"

&#x20;     }

&#x20;   ]

&#x20; }

}

3\. 注意事项

此处返回的 file\_path 必须是基于 C:\\mnt\\shared\\... 约定的路径。由于系统最终运行在 Windows 容器与局域网内，下游 D 模块通过挂载相同的存储卷，即可通过该绝对路径直接读取文件，无需走网络流下载。







