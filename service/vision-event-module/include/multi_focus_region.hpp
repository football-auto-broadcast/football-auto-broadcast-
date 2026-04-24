/**
 * multi_focus_region.hpp - 双机位关注区域集合定义
 *
 * 定义 MultiFocusRegion 结构，融合 cam_01 和 cam_02 两路关注区域。
 * 冻结规则：regions 必须同时包含 cam_01 与 cam_02，不允许只推一路。
 */

#ifndef VISION_EVENT_MODULE_MULTI_FOCUS_REGION_HPP
#define VISION_EVENT_MODULE_MULTI_FOCUS_REGION_HPP

#include "focus_region.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vision {

/**
 * @brief 双机位关注区域集合
 *
 * 将两路机位的关注区域打包为统一结构，用于推送到下游模块。
 */
struct MultiFocusRegion {
    std::string match_id;                         ///< 比赛 ID
    int64_t timestamp_ms = 0;                     ///< 毫秒时间戳
    std::vector<FocusRegion> regions;             ///< 关注区域列表 (长度必须为 2)

    /**
     * @brief 获取指定机位的关注区域
     * @return 若找到返回指针，否则返回 nullptr
     */
    const FocusRegion* get_region(const std::string& camera_id) const;

    /**
     * @brief 获取 cam_01 的关注区域
     */
    const FocusRegion* main_region() const;

    /**
     * @brief 获取 cam_02 的关注区域
     */
    const FocusRegion* aux_region() const;

    /**
     * @brief 检查集合是否有效
     *
     * 有效条件：
     * - regions 长度为 2
     * - 同时包含 cam_01 与 cam_02
     * - 每路关注区域自身有效
     */
    bool is_valid() const;

    /**
     * @brief 将集合序列化为 JSON 字符串
     */
    std::string to_json() const;

    /**
     * @brief 从 JSON 字符串反序列化
     */
    static MultiFocusRegion from_json(const std::string& json);
};

} // namespace vision

#endif // VISION_EVENT_MODULE_MULTI_FOCUS_REGION_HPP
