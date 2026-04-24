#include <iostream>
#include <vector>
#include <string>

// 先写一个模拟的相机枚举，后面再换成真实SDK调用
struct CameraInfo {
    std::string serial;
    std::string model;
    bool isOnline;
};

std::vector<CameraInfo> ListCameras() {
    // 先返回一个模拟的相机列表，说明功能能跑
    return {
        {"SN123456", "Hikrobot MV-CA013-20GC", true},
        {"SN789012", "Hikrobot MV-CA013-20GC", false}
    };
}

int main()
{
    std::cout << "=== 相机枚举工具 ===" << std::endl;
    std::cout << "正在扫描设备..." << std::endl << std::endl;

    auto cameras = ListCameras();

    if (cameras.empty()) {
        std::cout << "未找到任何相机设备！" << std::endl;
    }
    else {
        std::cout << "找到 " << cameras.size() << " 台相机：" << std::endl;
        for (size_t i = 0; i < cameras.size(); ++i) {
            std::cout << "[" << i + 1 << "] "
                << "序列号: " << cameras[i].serial
                << " | 型号: " << cameras[i].model
                << " | 状态: " << (cameras[i].isOnline ? "在线" : "离线")
                << std::endl;
        }
    }

    std::cout << std::endl << "按任意键退出..." << std::endl;
    std::cin.get();
    return 0;
}