#pragma once

#include <nlohmann/json.hpp>

#include <string>

enum class JobStatus { Queued, Running, Succeeded, Failed };

struct Job {
    std::string id;
    std::string type;
    std::string payload;
    JobStatus status = JobStatus::Queued;
};

void to_json(nlohmann::json& j, const Job& job);
void from_json(const nlohmann::json& j, Job& job);