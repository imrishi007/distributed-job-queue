#include "job.h"

#include <random>
#include <stdexcept>
#include <string>

namespace {

const char* status_name(JobStatus status) {
    switch (status) {
        case JobStatus::Queued: return "queued";
        case JobStatus::Running: return "running";
        case JobStatus::Succeeded: return "succeeded";
        case JobStatus::Failed: return "failed";
    }
    return "unknown";
}

JobStatus status_from_name(const std::string& name) {
    if (name == "queued") return JobStatus::Queued;
    if (name == "running") return JobStatus::Running;
    if (name == "succeeded") return JobStatus::Succeeded;
    if (name == "failed") return JobStatus::Failed;
    throw std::invalid_argument("unknown job status: " + name);
}

}  // namespace

std::string generate_job_id() {
    // ponytail: 64 random bits; fine for dev scale, upgrade to a UUID when
    // the queue outgrows ~thousands of concurrent jobs.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> hex(0, 15);
    static constexpr char digits[] = "0123456789abcdef";

    std::string id(16, '\0');
    for (char& c : id) {
        c = digits[hex(gen)];
    }
    return id;
}

void to_json(nlohmann::json& j, const Job& job) {
    j = nlohmann::json{{"id", job.id},
                       {"type", job.type},
                       {"payload", job.payload},
                       {"status", status_name(job.status)}};
}

void from_json(const nlohmann::json& j, Job& job) {
    job.id = j.at("id").get<std::string>();
    job.type = j.at("type").get<std::string>();
    job.payload = j.at("payload").get<std::string>();
    job.status = status_from_name(j.at("status").get<std::string>());
}