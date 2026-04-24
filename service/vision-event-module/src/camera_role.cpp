/**
 * camera_role.cpp - 机位角色实现
 *
 * 实现相机角色枚举和职责描述。
 */

#include "camera_role.hpp"

namespace vision {

const char* camera_role_to_string(CameraRole role) {
    switch (role) {
        case CameraRole::MAIN: return "main";
        case CameraRole::AUX:  return "aux";
        default:               return "unknown";
    }
}

CameraRole camera_role_from_string(const std::string& str) {
    if (str == "main") return CameraRole::MAIN;
    if (str == "aux")  return CameraRole::AUX;
    return CameraRole::MAIN;
}

CameraRole camera_id_to_role(const std::string& camera_id) {
    if (camera_id == "cam_02") return CameraRole::AUX;
    return CameraRole::MAIN;
}

std::string camera_role_to_id(CameraRole role) {
    switch (role) {
        case CameraRole::MAIN: return "cam_01";
        case CameraRole::AUX:  return "cam_02";
        default:               return "cam_01";
    }
}

std::string camera_role_description(CameraRole role) {
    switch (role) {
        case CameraRole::MAIN:
            return "cam_01 - 高位广角主机位: 全场覆盖、主画面全局关注区域、"
                   "全场运动趋势分析、主时间线候选事件基础检测、默认主画面来源";
        case CameraRole::AUX:
            return "cam_02 - 门线延长线辅机位: 门前近景观察、球门区域活动增强、"
                   "进球候选辅助确认、小禁区精彩瞬间候选增强、门前细节叙事";
        default:
            return "unknown";
    }
}

} // namespace vision
