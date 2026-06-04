/**
 * multi_focus_region.cpp - 多关注区域实现
 *
 * 融合双机位关注区域并输出统一结果。
 * 冻结规则：regions 必须同时包含 cam_01 与 cam_02。
 */

#include "multi_focus_region.hpp"

#include <algorithm>
#include <sstream>

namespace vision {

const FocusRegion* MultiFocusRegion::get_region(const std::string& camera_id) const {
    auto it = std::find_if(regions.begin(), regions.end(),
        [&camera_id](const FocusRegion& r) { return r.camera_id == camera_id; });
    if (it != regions.end()) {
        return &(*it);
    }
    return nullptr;
}

const FocusRegion* MultiFocusRegion::main_region() const {
    return get_region("cam_01");
}

const FocusRegion* MultiFocusRegion::aux_region() const {
    return get_region("cam_02");
}

bool MultiFocusRegion::is_valid() const {
    // 必须包含恰好 2 路区域
    if (regions.size() != 2) return false;

    // 必须同时包含 cam_01 与 cam_02
    bool has_cam_01 = false;
    bool has_cam_02 = false;
    for (const auto& region : regions) {
        if (region.camera_id == "cam_01") has_cam_01 = true;
        if (region.camera_id == "cam_02") has_cam_02 = true;
        if (!region.is_valid()) return false;
    }

    return has_cam_01 && has_cam_02;
}

std::string MultiFocusRegion::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"match_id\":\"" << match_id << "\",";
    oss << "\"timestamp_ms\":" << timestamp_ms << ",";
    oss << "\"regions\":[";
    for (size_t i = 0; i < regions.size(); ++i) {
        if (i > 0) oss << ",";
        oss << regions[i].to_json();
    }
    oss << "]}";
    return oss.str();
}

MultiFocusRegion MultiFocusRegion::from_json(const std::string& json) {
    // TODO: 实现 JSON 反序列化
    MultiFocusRegion result;
    return result;
}

} // namespace vision
