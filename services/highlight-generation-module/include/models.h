#pragma once
#include <string>
#include <vector>
#include <map>

// 系统统一错误码定义 (对接团队文档 7.4)
enum ErrorCode {
    SUCCESS = 0,
    ERR_PARAM = 1001,
    ERR_NOT_INIT = 1002,
    ERR_INPUT_UNAVAILABLE = 1003,
    ERR_STREAM_EXCEPTION = 1004,
    ERR_WRITE_FAILED = 1005,
    ERR_READ_FAILED = 1006,
    ERR_TASK_FAILED = 1007,
    ERR_CONFIG = 1011
};

// 视觉事件结构体 (对接 C 模块 v1.1 规约)
struct VisionEvent {
    std::string event_id;
    std::string event_type; // goal_candidate, shot_candidate, danger_attack_candidate, celebration_candidate
    double start_sec = 0.0;
    double end_sec = 0.0;
    double confidence = 0.0;
    std::string camera_id;
    double score = 0.0;     // 动态计算的优先级得分
};

// 录像文件索引模型 (对接 B 模块 8.7 规约)
struct RecordIndex {
    std::string match_id;
    double duration_sec = 0.0;
    std::string cam_01_raw_path;
    std::string cam_02_raw_path;
    std::string program_path;
    std::string status;
};

// 剪辑缓冲配置策略 (对接 E 模块 8.6 规约)
struct ClipPolicy {
    double pre_sec = 0.0;
    double post_sec = 0.0;
};

// 任务全局状态查询模型
struct TaskStatus {
    int code = 1002;
    std::string status = "idle"; // idle, processing, success, failed
    std::string message = "Initial state";
    std::string output_path = "";
};