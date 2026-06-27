/**
 * service.hpp - 视觉分析服务主接口
 *
 * 定义 VisionService 类，管理服务生命周期、比赛状态、
 * 帧处理、关注区域生成、多机位决策和事件检测。
 */

#ifndef VISION_EVENT_MODULE_SERVICE_HPP
#define VISION_EVENT_MODULE_SERVICE_HPP

#include "frame_input.hpp"
#include "event_types.hpp"
#include "focus_region.hpp"
#include "multi_focus_region.hpp"
#include "program_decision.hpp"
#include "fusion_policy.hpp"
#include "json_output.hpp"

#include <memory>
#include <string>

namespace vision {

/**
 * @brief 视觉分析服务
 *
 * 核心服务类，负责：
 * - 初始化和管理模块生命周期
 * - 接收和处理双机位视频帧
 * - 生成关注区域和多机位决策
 * - 检测候选事件
 * - 通过 HTTP API 对外提供服务
 */
class VisionService {
public:
    explicit VisionService(const std::string& config_path);
    ~VisionService();

    // 禁止拷贝
    VisionService(const VisionService&) = delete;
    VisionService& operator=(const VisionService&) = delete;

    /**
     * @brief 初始化服务
     * @return 初始化是否成功
     */
    bool initialize();

    /**
     * @brief 运行服务（阻塞直到 stop 被调用）
     */
    void run();

    /**
     * @brief 停止服务
     */
    void stop();

    /**
     * @brief 获取当前服务状态
     */
    ModuleState state() const;

    /**
     * @brief 获取指定比赛状态
     */
    ModuleStatus get_status(const std::string& match_id) const;

    /**
     * @brief 返回最近初始化或运行的比赛 ID，用于模块级状态查询兼容接口
     */
    std::string latest_match_id() const;

    /**
     * @brief 按模拟输入推进一帧双机位分析，用于无 RTSP/无模型联调
     */
    void process_simulated_frames();

    /**
     * @brief 从配置的 RTSP 视频流读取并处理一帧双机位输入
     */
    bool process_realtime_stream_frames();

    // ========================================================================
    // 比赛管理
    // ========================================================================

    /**
     * @brief 初始化一场比赛的视觉分析任务
     */
    bool init_match(const std::string& match_id);

    /**
     * @brief 开始一场比赛的视觉分析
     */
    bool start_match(const std::string& match_id);

    /**
     * @brief 配置一路输入视频流
     */
    bool configure_stream(const std::string& camera_id, const std::string& stream_uri);

    /**
     * @brief 配置元数据输出根目录，用于与 E/B/D 的 match 级产物路径对齐
     */
    bool configure_metadata_root(const std::string& metadata_root);

    /**
     * @brief 配置比赛录制时间零点，优先使用 B 模块首帧写入时间
     */
    bool configure_record_time_base(const std::string& match_id, int64_t timestamp_ms);

    /**
     * @brief 配置运行期融合输出选项
     */
    void configure_fusion_runtime(bool enable_dual_focus_regions,
                                  bool enable_program_decision,
                                  int focus_region_update_ms);

    /**
     * @brief 配置事件开关
     */
    void configure_event_runtime(bool enable_goal_candidate,
                                 bool enable_shot_candidate,
                                 bool enable_danger_attack_candidate,
                                 bool enable_celebration_candidate);

    /**
     * @brief 记录默认区域策略，用于合同初始化语义追踪
     */
    bool configure_default_region_policy(const std::string& camera_id,
                                         const std::string& policy);

    /**
     * @brief 停止一场比赛的视觉分析
     */
    bool stop_match(const std::string& match_id);

    /**
     * @brief 将候选事件写入 event_candidates.json
     */
    bool write_event_candidates(const std::string& match_id);

    // ========================================================================
    // 帧处理
    // ========================================================================

    /**
     * @brief 处理一帧输入
     */
    void process_frame(const InputFrame& frame);

    // ========================================================================
    // 关注区域生成
    // ========================================================================

    /**
     * @brief 生成双机位关注区域
     */
    MultiFocusRegion generate_focus_regions(const std::string& match_id);

    // ========================================================================
    // 多机位决策
    // ========================================================================

    /**
     * @brief 生成多机位决策结果
     */
    ProgramDecision generate_program_decision(const std::string& match_id);

    // ========================================================================
    // 事件检测
    // ========================================================================

    /**
     * @brief 获取候选事件列表
     */
    EventList get_event_candidates(const std::string& match_id);

private:
    void publish_outputs();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vision

#endif // VISION_EVENT_MODULE_SERVICE_HPP
