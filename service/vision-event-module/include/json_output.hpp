/**
 * json_output.hpp - JSON 输出结构定义
 *
 * 定义统一的 API 响应格式和错误码。
 * 所有 HTTP API 统一返回 { "code": 0, "message": "ok", "data": {} } 格式。
 */

#ifndef VISION_EVENT_MODULE_JSON_OUTPUT_HPP
#define VISION_EVENT_MODULE_JSON_OUTPUT_HPP

#include <cstdint>
#include <string>

namespace vision {

/**
 * @brief 错误码定义
 */
enum class ErrorCode : int {
    OK = 0,                       ///< 成功
    ERR_PARAM = 1001,             ///< 参数错误
    ERR_NOT_INITIALIZED = 1002,   ///< 资源未初始化
    ERR_SOURCE_UNAVAILABLE = 1003,///< 输入源不可用
    ERR_STREAM_EXCEPTION = 1004,  ///< 视频流异常
    ERR_FILE_WRITE_FAILED = 1005, ///< 文件写入失败
    ERR_FILE_READ_FAILED = 1006,  ///< 文件读取失败
    ERR_TASK_FAILED = 1007,       ///< 任务执行失败
    ERR_UPSTREAM_UNREACHABLE = 1008, ///< 上游服务不可达
    ERR_NOT_FOUND = 1009,         ///< 资源不存在
    ERR_STATE_CONFLICT = 1010,    ///< 状态冲突
    ERR_CONFIG = 1011,            ///< 配置错误
    ERR_SDK_INIT_FAILED = 1012,   ///< 工业相机 SDK 初始化失败
    ERR_ENUM_FAILED = 1013,       ///< 工业相机枚举失败
    ERR_STREAM_FAILED = 1014,     ///< 相机取流失败
    ERR_CAMERA_NOT_BOUND = 1015,  ///< 相机未绑定
    ERR_CAMERA_SERIAL_CONFLICT = 1016, ///< 相机序列号冲突
    ERR_FOCUS_REGION_EXPIRED = 1017, ///< 关注区域过期
    ERR_DECISION_TIMEOUT = 1018,  ///< 决策超时
    ERR_DISK_SPACE_LOW = 1019     ///< 磁盘空间不足
};

/**
 * @brief 将错误码转换为字符串描述
 */
const char* error_code_to_string(ErrorCode code);

/**
 * @brief 通用 API 响应结构
 *
 * 所有 HTTP API 统一返回此格式。
 */
struct ApiResponse {
    int code = 0;                 ///< 错误码，0 表示成功
    std::string message;          ///< 消息描述
    std::string data;             ///< 数据部分 (JSON 字符串)

    /**
     * @brief 创建成功响应
     */
    static ApiResponse ok(const std::string& data = "{}");

    /**
     * @brief 创建错误响应
     */
    static ApiResponse error(ErrorCode code, const std::string& message = "");

    /**
     * @brief 将响应序列化为 JSON 字符串
     */
    std::string to_json() const;
};

/**
 * @brief 模块状态
 */
enum class ModuleState : int {
    IDLE = 0,
    INITIALIZING = 1,
    RUNNING = 2,
    DEGRADED = 3,
    STOPPED = 4,
    FAILED = 5
};

/**
 * @brief 将 ModuleState 枚举转换为字符串
 */
const char* module_state_to_string(ModuleState state);

/**
 * @brief 模块状态响应结构
 */
struct ModuleStatus {
    std::string match_id;                     ///< 比赛 ID
    ModuleState status = ModuleState::IDLE;   ///< 模块状态
    std::string camera_main_status;           ///< 主机位状态: "online" / "offline"
    std::string camera_aux_status;            ///< 辅机位状态: "online" / "offline"
    int fps_main = 0;                         ///< 主机位帧率
    int fps_aux = 0;                          ///< 辅机位帧率
    int events_detected = 0;                  ///< 已检测事件数
    bool focus_region_cam_01_ready = false;   ///< 主机位关注区域是否就绪
    bool focus_region_cam_02_ready = false;   ///< 辅机位关注区域是否就绪
    std::string last_program_decision_camera; ///< 最近一次推荐机位
    int64_t last_focus_region_timestamp_ms = 0; ///< 最近关注区域时间戳
    int64_t last_decision_timestamp_ms = 0;    ///< 最近决策时间戳
    std::string error_message;                ///< 最近错误信息

    /**
     * @brief 将状态序列化为 JSON 字符串
     */
    std::string to_json() const;
};

} // namespace vision

#endif // VISION_EVENT_MODULE_JSON_OUTPUT_HPP
