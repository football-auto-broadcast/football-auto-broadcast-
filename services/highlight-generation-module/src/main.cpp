#include <iostream>
#include <cstdlib>
#include <string>

int main() {
    // 这里的 raw_match.mp4 需要你放一个测试视频在生成的 .exe 同级目录下
    std::string command = "ffmpeg -y -ss 00:00:05 -t 10 -i raw_match.mp4 -c copy test_highlight.mp4";
    std::cout << "--- [模块 D] 正在生成高光集锦... ---" << std::endl;

    int result = std::system(command.c_str());
    if (result == 0) std::cout << "生成成功！" << std::endl;
    else std::cout << "生成失败，请检查 FFmpeg 环境变量。" << std::endl;

    return 0;
}