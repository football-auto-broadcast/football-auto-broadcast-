/**
 * focus_region.hpp - 单路关注区域定义
 *
 * 定义单路关注区域结构 FocusRegion，用于主画面裁切的推荐区域。
 * 每路机位（cam_01 / cam_02）独立输出自己的关注区域。
 */

#ifndef VISION_EVENT_MODULE_FOCUS_REGION_HPP
#define VISION_EVENT_MODULE_FOCUS_REGION_HPP

#include <cstdint>
#include <string>

namespace vision {

/**
 * @brief 关注区域来源类型枚举（冻结）
 *
 * 当前版本仅支持以下 3 种来源类型。
 */
enum class FocusRegionSource : int {
    BALL_DETECTION = 0,     ///< 基于足球检测
    MOTION_CLUSTER = 1,     ///< 基于运动聚集
    DEFAULT = 2             ///< 默认回退区域
};

/**
 * @brief 将 FocusRegionSource 枚举转换为字符串
 */
const char* focus_region_source_to_string(FocusRegionSource source);

/**
 * @brief 关注区域来源类型从字符串解析
 */
FocusRegionSource focus_region_source_from_string(const std::string& str);

/**
 * @brief 矩形区域坐标
 */
struct Rect {
    int x = 0;          ///< 左上角 X 坐标 (像素)
    int y = 0;          ///< 左上角 Y 坐标 (像素)
    int width = 0;      ///< 区域宽度 (像素)
    int height = 0;     ///< 区域高度 (像素)

    /**
     * @brief 检查矩形是否有效 (width > 0 && height > 0)
     */
    bool is_valid() const;

    /**
     * @brief 计算区域面积
     */
    int area() const;
};

/**
 * @brief 单路关注区域
 *
 * 描述一路机位的推荐裁切区域，包含坐标、来源类型和置信度。
 */
struct FocusRegion {
    std::string camera_id;              ///< 来源机位: "cam_01" 或 "cam_02"
    Rect rect;                          ///< 关注区域矩形
    FocusRegionSource source_type;      ///< 来源类型
    double confidence = 0.0;            ///< 置信度 [0, 1]

    /**
     * @brief 检查关注区域是否有效
     */
    bool is_valid() const;

    /**
     * @brief 将关注区域序列化为 JSON 字符串
     */
    std::string to_json() const;

    /**
     * @brief 从 JSON 字符串反序列化
     */
    static FocusRegion from_json(const std::string& json);
};

} // namespace vision

#endif // VISION_EVENT_MODULE_FOCUS_REGION_HPP
