#pragma once
#include <string>
#include <vector>
#include <json.hpp>

using json = nlohmann::json;

// 比赛对象
struct Match {
    std::string match_id;
    std::string match_name;
    std::string status;
    int64_t created_at;
    int64_t started_at;
    int64_t finished_at;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Match,
    match_id, match_name, status,
    created_at, started_at, finished_at
)

// 模块状态对象
struct ModuleStatus {
    std::string module_name;
    std::string status;
    std::string last_error;
    int64_t last_checked;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ModuleStatus,
    module_name, status, last_error, last_checked
)

// 任务对象
struct Task {
    std::string task_id;
    std::string task_type;
    std::string match_id;
    std::string status;
    int progress;
    std::string result_path;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Task,
    task_id, task_type, match_id, status, progress, result_path
)