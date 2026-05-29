#pragma once
#include "models.h"
#include <string>
#include <vector>

class HighlightPipeline {
private:
    std::string ffmpegPath;
    std::string dataRoot;
public:
    HighlightPipeline();
    void loadConfiguration(const std::string& configPath);
    int executeWorkflow(const std::string& matchId, const std::string& recordIdxPath, 
                        const std::string& eventPath, const std::map<std::string, ClipPolicy>& targetPolicies, 
                        double maxDurationSec, std::string& outFinalPath);
};