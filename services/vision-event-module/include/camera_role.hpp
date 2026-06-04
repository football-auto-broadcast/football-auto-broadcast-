/**
 * camera_role.hpp - 机位角色定义
 *
 * 定义 cam_01（高位广角主机位）和 cam_02（门线延长线辅机位）的角色枚举及职责描述。
 */

#ifndef VISION_EVENT_MODULE_CAMERA_ROLE_HPP
#define VISION_EVENT_MODULE_CAMERA_ROLE_HPP

#include <string>

namespace vision {

/**
 * @brief 机位角色枚举
 */
enum class CameraRole : int {
    MAIN = 0,     ///< cam_01 - 高位广角主机位
    AUX = 1       ///< cam_02 - 门线延长线辅机位
};

/**
 * @brief 将 CameraRole 枚举转换为字符串
 */
const char* camera_role_to_string(CameraRole role);

/**
 * @brief 从字符串解析 CameraRole
 */
CameraRole camera_role_from_string(const std::string& str);

/**
 * @brief 从 camera_id 字符串获取机位角色
 */
CameraRole camera_id_to_role(const std::string& camera_id);

/**
 * @brief 从机位角色获取 camera_id 字符串
 */
std::string camera_role_to_id(CameraRole role);

/**
 * @brief 获取机位角色的职责描述
 */
std::string camera_role_description(CameraRole role);

} // namespace vision

#endif // VISION_EVENT_MODULE_CAMERA_ROLE_HPP
