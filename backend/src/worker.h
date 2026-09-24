#pragma once

#include <nlohmann/json.hpp>

#include <string>

enum class WorkerStatus { Voluntary, Busy };

struct Worker {
    std::string id;
    std::string name;
    WorkerStatus status = WorkerStatus::Voluntary;
    std::string last_seen;
};

const char* worker_status_name(WorkerStatus status);

void to_json(nlohmann::json& j, const Worker& w);