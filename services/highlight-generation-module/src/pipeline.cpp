#include "../include/pipeline.h"
#include "../include/json_utils.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <cstdlib>

namespace fs = std::filesystem;

HighlightPipeline::HighlightPipeline() {
    ffmpegPath = "third_party/windows/ffmpeg/bin/ffmpeg.exe"; // 默认安全路径
    dataRoot = "D:\\football\\data";
}

void HighlightPipeline::loadConfiguration(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("ffmpeg_path") != std::string::npos) {
            ffmpegPath = JsonUtils::trim(line.substr(line.find("=") + 1));
        }
        if (line.find("data_root") != std::string::npos) {
            dataRoot = JsonUtils::trim(line.substr(line.find("=") + 1));
        }
    }
    file.close();
}

int HighlightPipeline::executeWorkflow(const std::string& matchId, const std::string& recordIdxPath, 
                                        const std::string& eventPath, const std::map<std::string, ClipPolicy>& targetPolicies, 
                                        double maxDurationSec, std::string& outFinalPath) {
    // 1. 物理依赖路径检查拦截
    if (!fs::exists(eventPath)) return ERR_PARAM;
    if (!fs::exists(recordIdxPath)) return ERR_READ_FAILED;
    if (!fs::exists(ffmpegPath)) return ERR_CONFIG;

    // 2. 解析资产元数据
    std::vector<VisionEvent> rawEvents = JsonUtils::parseEvents(eventPath);
    RecordIndex fileIndex = JsonUtils::parseRecordIndex(recordIdxPath);
    if (rawEvents.empty()) return ERR_INPUT_UNAVAILABLE;

    // 3. 数据流清洗与边界值检测 (时间窗合法性校验)
    std::vector<VisionEvent> validEvents;
    for (auto& ev : rawEvents) {
        double threshold = (ev.event_type == "goal_candidate") ? 0.70 : 
                           (ev.event_type == "shot_candidate") ? 0.75 : 0.80;
        if (ev.confidence < threshold) continue;

        // 应用前后加缓冲缓冲控制算法 (对接 E 模块策略)
        double pre = 5.0, post = 5.0;
        if (targetPolicies.count(ev.event_type)) {
            pre = targetPolicies.at(ev.event_type).pre_sec;
            post = targetPolicies.at(ev.event_type).post_sec;
        }
        ev.start_sec = std::max(0.0, ev.start_sec - pre);
        ev.end_sec = (fileIndex.duration_sec > 0) ? std::min(fileIndex.duration_sec, ev.end_sec + post) : ev.end_sec + post;
        
        if (ev.end_sec > ev.start_sec) {
            validEvents.push_back(ev);
        }
    }

    // 4. 完成候选事件去重处理 (时域重叠状态机吞噬算法)
    std::sort(validEvents.begin(), validEvents.end(), [](const VisionEvent& a, const VisionEvent& b) {
        return (a.event_type == b.event_type) ? (a.start_sec < b.start_sec) : (a.event_type < b.event_type);
    });

    std::vector<VisionEvent> deduped;
    for (const auto& ev : validEvents) {
        if (deduped.empty() || deduped.back().event_type != ev.event_type) {
            deduped.push_back(ev);
        } else {
            auto& last = deduped.back();
            if (std::max(last.start_sec, ev.start_sec) <= std::min(last.end_sec, ev.end_sec)) {
                last.start_sec = std::min(last.start_sec, ev.start_sec);
                last.end_sec = std::max(last.end_sec, ev.end_sec);
                last.confidence = std::max(last.confidence, ev.confidence);
            } else {
                deduped.push_back(ev);
            }
        }
    }

    // 5. 跨异类关联级联去重业务逻辑 (进球吞噬周边噪声)
    for (auto& goal : deduped) {
        if (goal.event_type != "goal_candidate") continue;
        for (auto it = deduped.begin(); it != deduped.end(); ) {
            if ((it->event_type == "celebration_candidate" && it->start_sec >= goal.end_sec && it->start_sec <= goal.end_sec + 30.0) ||
                (it->event_type == "shot_candidate" && it->start_sec >= goal.start_sec - 10.0 && it->start_sec <= goal.start_sec)) {
                goal.end_sec = std::max(goal.end_sec, it->end_sec);
                it = deduped.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 6. 优先级评分排序模型排序 (Goal=100, Shot=70, Danger=50, Celebration=30)
    for (auto& ev : deduped) {
        double base = (ev.event_type == "goal_candidate") ? 100.0 :
                      (ev.event_type == "shot_candidate") ? 70.0 :
                      (ev.event_type == "danger_attack_candidate") ? 50.0 : 30.0;
        ev.score = base * ev.confidence;
    }
    std::sort(deduped.begin(), deduped.end(), [](const VisionEvent& a, const VisionEvent& b) { return a.score > b.score; });

    // 7. 动态高光时长容量熔断算法控制
    double currentTotal = 0.0;
    std::vector<VisionEvent> finalCulled;
    for (const auto& ev : deduped) {
        double len = ev.end_sec - ev.start_sec;
        if (currentTotal + len <= maxDurationSec) {
            finalCulled.push_back(ev);
            currentTotal += len;
        }
    }
    if (finalCulled.empty()) return ERR_TASK_FAILED;

    // 8. 视频寻址路由与双轨容错安全剪辑机制
    std::string tempDir = dataRoot + "\\temp\\" + matchId + "\\";
    fs::create_directories(tempDir);
    std::string listPath = tempDir + "concat_list.txt";
    std::ofstream listFile(listPath);

    for (size_t i = 0; i < finalCulled.size(); ++i) {
        const auto& ev = finalCulled[i];
        std::string targetMedia = "";

        // 核心规则：优先选用 B 模块生成的主画面 Program 录像进行高质剪辑
        if (!fileIndex.program_path.empty() && fs::exists(fileIndex.program_path)) {
            targetMedia = fileIndex.program_path;
        } 
        // 容错路由：若主画面破损缺失，秒级回退至对应的机位原始物理文件
        else {
            std::cout << "[WARN] Main video missing. Fallback to raw footage active." << std::endl;
            if (ev.camera_id == "cam_02" && fs::exists(fileIndex.cam_02_raw_path)) targetMedia = fileIndex.cam_02_raw_path;
            else if (fs::exists(fileIndex.cam_01_raw_path)) targetMedia = fileIndex.cam_01_raw_path;
        }

        if (targetMedia.empty()) {
            listFile.close();
            fs::remove_all(tempDir);
            return ERR_INPUT_UNAVAILABLE;
        }

        std::stringstream ss;
        ss << tempDir << "part_" << std::setw(3) << std::setfill('0') << i << ".mp4";
        std::string partPath = ss.str();

        // FFmpeg Stream Copy 无损高速裁剪核心原语
        std::stringstream cmd;
        cmd << "\"" << ffmpegPath << "\" -ss " << ev.start_sec << " -t " << (ev.end_sec - ev.start_sec)
            << " -i \"" << targetMedia << "\" -c copy -avoid_negative_ts 1 -y \"" << partPath << "\" > NUL 2>&1";

        if (std::system(cmd.str().c_str()) != 0) {
            listFile.close();
            return ERR_STREAM_EXCEPTION;
        }
        listFile << "file '" << fs::absolute(partPath).generic_string() << "'\n";
    }
    listFile.close();

    // 9. 终极无损拼接缝合生成
    std::string outDir = dataRoot + "\\output\\" + matchId + "\\";
    fs::create_directories(outDir);
    outFinalPath = outDir + "full_highlight.mp4";

    std::stringstream concatCmd;
    concatCmd << "\"" << ffmpegPath << "\" -f concat -safe 0 -i \"" << fs::absolute(listPath).generic_string()
              << "\" -c copy -movflags faststart -y \"" << outFinalPath << "\" > NUL 2>&1";

    int ret = std::system(concatCmd.str().c_str());
    fs::remove_all(tempDir); // 物理擦除临时现场碎片

    return (ret == 0) ? SUCCESS : ERR_STREAM_EXCEPTION;
}